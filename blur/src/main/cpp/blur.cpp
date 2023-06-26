//
// Created by jesp on 2023-06-26.
//

#include "blur.h"
#include <GLES3/gl31.h>
#include <GLES3/gl3ext.h>
#include <android/bitmap.h>
#include <android/log.h>
#include <cmath>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <vector>
#include <functional>
#include <queue>
#include <thread>
#include <future>

#define LOG_TAG "Native Blur"
#define ANDROID_LOG_DEBUG 3
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

using namespace std;

namespace ThreadPool {
    class ThreadPool {
    public:
        explicit ThreadPool(size_t num_threads);

        ~ThreadPool();

        // job 을 추가한다.
        template<class F, class... Args>
        std::future<typename std::result_of<F(Args...)>::type> EnqueueJob(
                F &&f, Args &&... args);

    private:
        // 총 Worker 쓰레드의 개수.
        size_t num_threads_;
        // Worker 쓰레드를 보관하는 벡터.
        std::vector<std::thread> worker_threads_;
        // 할일들을 보관하는 job 큐.
        std::queue<std::function<void()>> jobs_;
        // 위의 job 큐를 위한 cv 와 m.
        std::condition_variable cv_job_q_;
        std::mutex m_job_q_;

        // 모든 쓰레드 종료
        bool stop_all;

        // Worker 쓰레드
        void WorkerThread();
    };

    ThreadPool::ThreadPool(size_t num_threads)
            : num_threads_(num_threads), stop_all(false) {
        worker_threads_.reserve(num_threads_);
        for (size_t i = 0; i < num_threads_; ++i) {
            worker_threads_.emplace_back([this]() { this->WorkerThread(); });
        }
    }

    void ThreadPool::WorkerThread() {
        while (true) {
            std::unique_lock<std::mutex> lock(m_job_q_);
            cv_job_q_.wait(lock, [this]() { return !this->jobs_.empty() || stop_all; });
            if (stop_all && this->jobs_.empty()) {
                return;
            }

            // 맨 앞의 job 을 뺀다.
            std::function<void()> job = std::move(jobs_.front());
            jobs_.pop();
            lock.unlock();

            // 해당 job 을 수행한다 :)
            job();
        }
    }

    ThreadPool::~ThreadPool() {
        stop_all = true;
        cv_job_q_.notify_all();

        for (auto &t: worker_threads_) {
            t.join();
        }
    }

    template<class F, class... Args>
    std::future<typename std::result_of<F(Args...)>::type> ThreadPool::EnqueueJob(
            F &&f, Args &&... args) {
        if (stop_all) {
            throw std::runtime_error("ThreadPool 사용 중지됨");
        }

        using return_type = typename std::result_of<F(Args...)>::type;
        auto job = std::make_shared<std::packaged_task<return_type()>>(
                [Func = std::forward<F>(f)] { return Func(); });
        std::future<return_type> job_result_future = job->get_future();
        {
            std::lock_guard<std::mutex> lock(m_job_q_);
            jobs_.push([job]() { (*job)(); });
        }
        cv_job_q_.notify_one();

        return job_result_future;
    }

}


void processingRow(const SharedValues *const sharedValues, unsigned int *imagePixels, const int startRow, const int endRow) {
    LOGD("processingRow startRow: %d, endRow: %d", startRow, endRow);
    long sumRed, sumGreen, sumBlue;
    long sumInputRed, sumInputGreen, sumInputBlue;
    long sumOutputRed, sumOutputGreen, sumOutputBlue;
    int startPixelIndex, inPixelIndex, outputPixelIndex;
    int stackStart, stackPointer, stackIndex;
    int colOffset;

    int red, green, blue;
    int multiplier;

    const int widthMax = sharedValues->widthMax;
    const int blurRadius = sharedValues->blurRadius;
    const int targetWidth = sharedValues->targetWidth;
    const int divisor = sharedValues->divisor;
    const int multiplySum = sharedValues->multiplySum;
    const int shiftSum = sharedValues->shiftSum;

    int *blurStack = new int[divisor];

    for (int row = startRow; row <= endRow; row++) {
        sumRed = sumGreen = sumBlue = sumInputRed = sumInputGreen = sumInputBlue = sumOutputRed = sumOutputGreen = sumOutputBlue = 0;
        startPixelIndex = row * targetWidth;
        inPixelIndex = startPixelIndex;
        stackIndex = blurRadius;

        for (int rad = 0; rad <= blurRadius; rad++) {
            stackIndex = rad;
            auto pixel = imagePixels[startPixelIndex];
            blurStack[stackIndex] = pixel;

            red = ((pixel >> 16) & 0xff);
            green = ((pixel >> 8) & 0xff);
            blue = (pixel & 0xff);

            multiplier = rad - 1;
            sumRed += red * multiplier;
            sumGreen += green * multiplier;
            sumBlue += blue * multiplier;

            sumOutputRed += red;
            sumOutputGreen += green;
            sumOutputBlue += blue;

            if (rad >= 1) {
                if (rad <= widthMax) inPixelIndex++;
                stackIndex = rad + blurRadius;
                pixel = imagePixels[inPixelIndex];
                blurStack[stackIndex] = pixel;

                multiplier = blurRadius + 1 - rad;

                red = ((pixel >> 16) & 0xff);
                green = ((pixel >> 8) & 0xff);
                blue = (pixel & 0xff);

                sumRed += red * multiplier;
                sumGreen += green * multiplier;
                sumBlue += blue * multiplier;

                sumInputRed += red;
                sumInputGreen += green;
                sumInputBlue += blue;
            }
        }

        stackStart = blurRadius;
        stackPointer = blurRadius;
        colOffset = blurRadius;
        if (colOffset > widthMax) colOffset = widthMax;
        inPixelIndex = colOffset + row * targetWidth;
        outputPixelIndex = startPixelIndex;

        for (int col = 0; col < targetWidth; col++) {
            imagePixels[outputPixelIndex] =
                    ((imagePixels[outputPixelIndex] bitand 0xff000000) bitor ((((sumRed * multiplySum) >> shiftSum) bitand 0xff) << 16) bitor
                     ((((sumGreen * multiplySum) >> shiftSum) bitand 0xff) << 8) bitor
                     (((sumBlue * multiplySum) >> shiftSum) bitand 0xff));

            outputPixelIndex++;
            sumRed -= sumOutputRed;
            sumGreen -= sumOutputGreen;
            sumBlue -= sumOutputBlue;

            stackStart = stackPointer + divisor - blurRadius;
            if (stackStart >= divisor) stackStart -= divisor;
            stackIndex = stackStart;

            sumOutputRed -= ((blurStack[stackIndex] >> 16) bitand 0xff);
            sumOutputGreen -= ((blurStack[stackIndex] >> 8) bitand 0xff);
            sumOutputBlue -= (blurStack[stackIndex] bitand 0xff);

            if (colOffset < widthMax) {
                inPixelIndex++;
                colOffset++;
            }

            auto pixel = imagePixels[inPixelIndex];
            blurStack[stackIndex] = pixel;

            red = ((pixel >> 16) bitand 0xff);
            green = ((pixel >> 8) bitand 0xff);
            blue = (pixel bitand 0xff);

            sumInputRed += red;
            sumInputGreen += green;
            sumInputBlue += blue;

            sumRed += sumInputRed;
            sumGreen += sumInputGreen;
            sumBlue += sumInputBlue;

            if (++stackPointer >= divisor) stackPointer = 0;
            stackIndex = stackPointer;

            int stackPixel = blurStack[stackIndex];

            red = ((stackPixel >> 16) bitand 0xff);
            green = ((stackPixel >> 8) bitand 0xff);
            blue = (stackPixel bitand 0xff);

            sumOutputRed += red;
            sumOutputGreen += green;
            sumOutputBlue += blue;

            sumInputRed -= red;
            sumInputGreen -= green;
            sumInputBlue -= blue;
        }
    }
    delete[] blurStack;
    LOGD("processingRow finished startRow: %d, endRow: %d", startRow, endRow);

}

void processingColumn(const SharedValues *const sharedValues, unsigned int *imagePixels, const int startColumn, const int endColumn) {
    LOGD("processingColumn startColumn: %d, endColumn: %d", startColumn, endColumn);

    const int heightMax = sharedValues->heightMax;
    const int blurRadius = sharedValues->blurRadius;
    const int targetWidth = sharedValues->targetWidth;
    const int targetHeight = sharedValues->targetHeight;
    const int divisor = sharedValues->divisor;
    const int multiplySum = sharedValues->multiplySum;
    const int shiftSum = sharedValues->shiftSum;

    int xOffset, yOffset, blurStackIndex, stackStart, stackIndex, stackPointer, sourceIndex, destinationIndex;

    long sumRed, sumGreen, sumBlue, sumInputRed, sumInputGreen, sumInputBlue, sumOutputRed, sumOutputGreen, sumOutputBlue;

    int red, green, blue;

    int *blurStack = new int[divisor];

    for (int col = startColumn; col <= endColumn; col++) {
        sumOutputBlue = sumOutputGreen = sumOutputRed = sumInputBlue = sumInputGreen = sumInputRed = sumBlue = sumGreen = sumRed = 0;
        sourceIndex = col;

        for (int rad = 0; rad <= blurRadius; rad++) {
            stackIndex = rad;
            int pixel = imagePixels[sourceIndex];
            blurStack[stackIndex] = pixel;

            red = ((pixel >> 16) bitand 0xff);
            green = ((pixel >> 8) bitand 0xff);
            blue = (pixel bitand 0xff);

            int multiplier = rad + 1;

            sumRed += red * multiplier;
            sumGreen += green * multiplier;
            sumBlue += blue * multiplier;

            sumOutputRed += red;
            sumOutputGreen += green;
            sumOutputBlue += blue;

            if (rad >= 1) {
                if (rad <= heightMax) sourceIndex += targetWidth;

                stackIndex = rad + blurRadius;
                pixel = imagePixels[sourceIndex];
                blurStack[stackIndex] = pixel;

                multiplier = blurRadius + 1 - rad;

                red = ((pixel >> 16) bitand 0xff);
                green = ((pixel >> 8) bitand 0xff);
                blue = (pixel bitand 0xff);

                sumRed += red * multiplier;
                sumGreen += green * multiplier;
                sumBlue += blue * multiplier;

                sumInputRed += red;
                sumInputGreen += green;
                sumInputBlue += blue;
            }
        }

        stackPointer = blurRadius;
        yOffset = min(blurRadius, heightMax);
        sourceIndex = col + yOffset * targetWidth;
        destinationIndex = col;

        for (int y = 0; y < targetHeight; y++) {
            imagePixels[destinationIndex] =
                    ((imagePixels[destinationIndex] bitand 0xff000000) bitor ((((sumRed * multiplySum) >> shiftSum) bitand 0xff) << 16) bitor (
                            (((sumGreen * multiplySum) >> shiftSum) bitand 0xff) << 8) bitor (((sumBlue * multiplySum) >> shiftSum) bitand 0xff));

            destinationIndex += targetWidth;
            sumRed -= sumOutputRed;
            sumGreen -= sumOutputGreen;
            sumBlue -= sumOutputBlue;

            stackStart = stackPointer + divisor - blurRadius;
            if (stackStart >= divisor) stackStart -= divisor;
            stackIndex = stackStart;

            sumOutputRed -= ((blurStack[stackIndex] >> 16) bitand 0xff);
            sumOutputGreen -= ((blurStack[stackIndex] >> 8) bitand 0xff);
            sumOutputBlue -= (blurStack[stackIndex] bitand 0xff);

            if (yOffset < heightMax) {
                sourceIndex += targetWidth;
                yOffset++;
            }

            blurStack[stackIndex] = imagePixels[sourceIndex];

            sumInputRed += ((imagePixels[sourceIndex] >> 16) bitand 0xff);
            sumInputGreen += ((imagePixels[sourceIndex] >> 8) bitand 0xff);
            sumInputBlue += (imagePixels[sourceIndex] bitand 0xff);

            sumRed += sumInputRed;
            sumGreen += sumInputGreen;
            sumBlue += sumInputBlue;

            if (++stackPointer >= divisor) stackPointer = 0;
            stackIndex = stackPointer;

            int stackPixel = blurStack[stackIndex];

            red = ((stackPixel >> 16) bitand 0xff);
            green = ((stackPixel >> 8) bitand 0xff);
            blue = (stackPixel bitand 0xff);

            sumOutputRed += red;
            sumOutputGreen += green;
            sumOutputBlue += blue;

            sumInputRed -= red;
            sumInputGreen -= green;
            sumInputBlue -= blue;
        }
    }
    delete[] blurStack;
    LOGD("processingColumn finished startColumn: %d, endColumn: %d", startColumn, endColumn);
}

void blur(unsigned int *imagePixels, const int radius, const int targetWidth, const int targetHeight) {
    const long availableThreads = sysconf(_SC_NPROCESSORS_ONLN);

    const int r = targetHeight / availableThreads;
    const int c = targetWidth / availableThreads;

    const auto *sharedValues = new SharedValues{targetWidth - 1, targetHeight - 1, radius * 2 + 1, MUL_TABLE[radius], SHR_TABLE[radius],
                                                targetWidth,
                                                targetHeight, radius};

    vector<function<void()>> rowWorks;
    vector<function<void()>> columnWorks;

    for (int i = 0; i < availableThreads; i++) {
        const int startRow = i * r;
        const int endRow = (i + 1) * r - 1;
        rowWorks.emplace_back([sharedValues, imagePixels, startRow, endRow] { return processingRow(sharedValues, imagePixels, startRow, endRow); });
    }

    for (int i = 0; i < availableThreads; i++) {
        const int startColumn = i * c;
        const int endColumn = (i + 1) * c - 1;
        columnWorks.emplace_back(
                [sharedValues, imagePixels, startColumn, endColumn] { return processingColumn(sharedValues, imagePixels, startColumn, endColumn); });
    }

    ThreadPool::ThreadPool pool(availableThreads);
    std::vector<std::future<void>> futures;

    for (const auto &row: rowWorks) {
        futures.emplace_back(pool.EnqueueJob(row));
    }

    for (auto &f: futures) {
        f.wait();
    }

    for (const auto &col: columnWorks) {
        futures.emplace_back(pool.EnqueueJob(col));
    }


    for (auto &f: futures) {
        f.wait();
    }

    delete sharedValues;
}


/*
package io.github.pknujsp.testbed.core.ui

import android.graphics.Bitmap
import android.graphics.Rect
import android.os.Handler
import android.os.Looper
import android.util.Size
import android.view.PixelCopy
import android.view.View
import android.view.Window
import androidx.annotation.Dimension
import androidx.annotation.FloatRange
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.DelicateCoroutinesApi
import kotlinx.coroutines.Job
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import kotlinx.coroutines.newFixedThreadPoolContext
import kotlinx.coroutines.plus
import kotlinx.coroutines.suspendCancellableCoroutine
import java.lang.ref.SoftReference
import java.lang.ref.WeakReference
import kotlin.coroutines.resume


class BlurProcessor {

  private companion object {

    private val scope = MainScope() + Job()

    private val mulTable = intArrayOf(
      512, 512, 456, 512, 328, 456, 335, 512, 405, 328, 271, 456, 388, 335, 292, 512,
      454, 405, 364, 328, 298, 271, 496, 456, 420, 388, 360, 335, 312, 292, 273, 512,
      482, 454, 428, 405, 383, 364, 345, 328, 312, 298, 284, 271, 259, 496, 475, 456,
      437, 420, 404, 388, 374, 360, 347, 335, 323, 312, 302, 292, 282, 273, 265, 512,
      497, 482, 468, 454, 441, 428, 417, 405, 394, 383, 373, 364, 354, 345, 337, 328,
      320, 312, 305, 298, 291, 284, 278, 271, 265, 259, 507, 496, 485, 475, 465, 456,
      446, 437, 428, 420, 412, 404, 396, 388, 381, 374, 367, 360, 354, 347, 341, 335,
      329, 323, 318, 312, 307, 302, 297, 292, 287, 282, 278, 273, 269, 265, 261, 512,
      505, 497, 489, 482, 475, 468, 461, 454, 447, 441, 435, 428, 422, 417, 411, 405,
      399, 394, 389, 383, 378, 373, 368, 364, 359, 354, 350, 345, 341, 337, 332, 328,
      324, 320, 316, 312, 309, 305, 301, 298, 294, 291, 287, 284, 281, 278, 274, 271,
      268, 265, 262, 259, 257, 507, 501, 496, 491, 485, 480, 475, 470, 465, 460, 456,
      451, 446, 442, 437, 433, 428, 424, 420, 416, 412, 408, 404, 400, 396, 392, 388,
      385, 381, 377, 374, 370, 367, 363, 360, 357, 354, 350, 347, 344, 341, 338, 335,
      332, 329, 326, 323, 320, 318, 315, 312, 310, 307, 304, 302, 299, 297, 294, 292,
      289, 287, 285, 282, 280, 278, 275, 273, 271, 269, 267, 265, 263, 261, 259,
    )

    private val shrTable = intArrayOf(
      9, 11, 12, 13, 13, 14, 14, 15, 15, 15, 15, 16, 16, 16, 16, 17,
      17, 17, 17, 17, 17, 17, 18, 18, 18, 18, 18, 18, 18, 18, 18, 19,
      19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 20, 20, 20,
      20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 21,
      21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
      21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 22, 22, 22, 22, 22, 22,
      22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22,
      22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 23,
      23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23,
      23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23,
      23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23,
      23, 23, 23, 23, 23, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
      24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
      24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
      24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
      24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
    )

    private val availableThreads = Runtime.getRuntime().availableProcessors() - 1

    @OptIn(DelicateCoroutinesApi::class) private val dispatchers = newFixedThreadPoolContext(availableThreads, "StackBlurThreads")
  }

  private data class SharedValues(
    val widthMax: Int,
    val heightMax: Int,
    val divisor: Int,
    val multiplySum: Int,
    val shiftSum: Int,
  )

suspend fun blur(
        view: View, window: Window, @Dimension(unit = Dimension.DP) radius: Int,
@FloatRange(from = 1.0, to = 5.0) resizeRatio: Double = 2.2,
): Result<Bitmap> = suspendCancellableCoroutine {
        scope.launch(dispatchers) {
            try {
                val originalSize = Size(view.width, view.height)
                val originalBitmap: Bitmap = WeakReference(Bitmap.createBitmap(originalSize.width, originalSize.height, Bitmap.Config.ARGB_8888)).get()!!

                        val copySuccess: Boolean = suspendCancellableCoroutine {
                    val locationOfViewInWindow = IntArray(2)
                    view.getLocationInWindow(locationOfViewInWindow)
                    val locationRect = WeakReference(
                    Rect(
                    locationOfViewInWindow[0],
                    locationOfViewInWindow[1],
                    locationOfViewInWindow[0] + originalSize.width,
                    locationOfViewInWindow[1] + originalSize.height,
                    ),
                    ).get()!!

                    PixelCopy.request(
                    window, locationRect, originalBitmap,
                    { result ->
                                it.resume(result == PixelCopy.SUCCESS)
                    },
                    Handler(Looper.getMainLooper()),
                    )
            }

                if (copySuccess) {
                    val reducedSize = Size(
                            (view.width / resizeRatio).toInt().let { if (it % 2 == 0) it else it - 1 },
                    (view.height / resizeRatio).toInt().let { if (it % 2 == 0) it else it - 1 },
                    )
                    val pixels = SoftReference(IntArray(reducedSize.width * reducedSize.height)).get()!!
                            val reducedBitmap = WeakReference(Bitmap.createScaledBitmap(originalBitmap, reducedSize.width, reducedSize.height, false)).get()!!
                            reducedBitmap.getPixels(pixels, 0, reducedSize.width, 0, 0, reducedSize.width, reducedSize.height)
                    originalBitmap.recycle()

                    val r = reducedSize.height / availableThreads
                    val c = reducedSize.width / availableThreads

                    val sharedValues = WeakReference(
                            SharedValues(
                                    widthMax = reducedSize.width - 1,
                                    heightMax = reducedSize.height - 1,
                                    divisor = radius * 2 + 1,
                                    multiplySum = mulTable[radius],
                                    shiftSum = shrTable[radius],
                            ),
                    ).get()!!

                            val rowWorks = Array(availableThreads) { thread ->
                                ProcessingRow(sharedValues, pixels, reducedSize, thread * r, (thread + 1) * r - 1, radius)
                    }
                    val columnWorks = Array(availableThreads) { thread ->
                                ProcessingColumn(sharedValues, pixels, reducedSize, thread * c, (thread + 1) * c - 1, radius)
                    }

                    rowWorks.map { rowWork ->
                                async {
                            rowWork()
                        }
                    }.awaitAll()

                    columnWorks.map { columnWork ->
                                async {
                            columnWork()
                        }
                    }.awaitAll()
                    reducedBitmap.setPixels(pixels, 0, reducedSize.width, 0, 0, reducedSize.width, reducedSize.height)
                    it.resume(Result.success(reducedBitmap))
                }
            } catch (e: Exception) {
                it.resume(Result.failure(e))
            }
        }
}

fun cancel() {
    try {
        scope.cancel(cause = CancellationException("Dismissed dialog"))
    } catch (e: Exception) {
        e.printStackTrace()
    }
}

private class ProcessingRow(
private val sharedValues: SharedValues,
private val imagePixels: IntArray,
private val bitmapSize: Size,
private val startRow: Int,
private val endRow: Int,
private val blurRadius: Int,
) {
operator fun invoke() {
    var sumRed: Long
    var sumGreen: Long
    var sumBlue: Long

    var sumInputRed: Long
    var sumInputGreen: Long
    var sumInputBlue: Long

    var sumOutputRed: Long
    var sumOutputGreen: Long
    var sumOutputBlue: Long

    var startPixelIndex: Int
    var inPixelIndex: Int
    var outputPixelIndex: Int
    var stackStart: Int
    var stackPointer: Int
    var stackIndex: Int
    var colOffset: Int

    var red: Int
    var green: Int
    var blue: Int

    var multiplier: Int

    sharedValues.run {
        val blurStack = WeakReference(IntArray(divisor)).get()!!

        for (row in startRow..endRow) {
            sumRed = 0
            sumGreen = 0
            sumBlue = 0

            sumInputRed = 0
            sumInputGreen = 0
            sumInputBlue = 0

            sumOutputRed = 0
            sumOutputGreen = 0
            sumOutputBlue = 0

            startPixelIndex = bitmapSize.width * row
            inPixelIndex = startPixelIndex
            stackIndex = blurRadius

            for (rad in 0..blurRadius) {
                stackIndex = rad
                var pixel = imagePixels[startPixelIndex]
                blurStack[stackIndex] = pixel

                red = ((pixel ushr 16) and 0xff)
                green = ((pixel ushr 8) and 0xff)
                blue = (pixel and 0xff)

                multiplier = rad + 1

                sumRed += red * multiplier
                sumGreen += green * multiplier
                sumBlue += blue * multiplier

                sumOutputRed += red
                sumOutputGreen += green
                sumOutputBlue += blue

                if (rad >= 1) {
                    if (rad <= widthMax) inPixelIndex++
                    stackIndex = rad + blurRadius
                    pixel = imagePixels[inPixelIndex]
                    blurStack[stackIndex] = pixel

                    multiplier = blurRadius + 1 - rad

                    red = ((pixel ushr 16) and 0xff)
                    green = ((pixel ushr 8) and 0xff)
                    blue = (pixel and 0xff)

                    sumRed += red * multiplier
                    sumGreen += green * multiplier
                    sumBlue += blue * multiplier

                    sumInputRed += red
                    sumInputGreen += green
                    sumInputBlue += blue
                }
            }

            stackStart = blurRadius
            stackPointer = blurRadius
            colOffset = blurRadius.coerceAtMost(widthMax)
            inPixelIndex = colOffset + row * bitmapSize.width
            outputPixelIndex = startPixelIndex

            for (col in 0 until bitmapSize.width) {
                imagePixels[outputPixelIndex] =
                        ((imagePixels[outputPixelIndex] and 0xff000000.toInt()) or ((((sumRed.toInt() * multiplySum) ushr shiftSum) and 0xff) shl 16) or ((((sumGreen.toInt() * multiplySum) ushr shiftSum) and 0xff) shl 8) or (((sumBlue.toInt() * multiplySum) ushr shiftSum) and 0xff))

                outputPixelIndex++
                sumRed -= sumOutputRed
                sumGreen -= sumOutputGreen
                sumBlue -= sumOutputBlue

                stackIndex = (stackPointer + divisor - blurRadius).let {
                    stackStart = if (it >= divisor) it - divisor else it
                                stackStart
                }

                sumOutputRed -= ((blurStack[stackIndex] ushr 16) and 0xff)
                sumOutputGreen -= ((blurStack[stackIndex] ushr 8) and 0xff)
                sumOutputBlue -= (blurStack[stackIndex] and 0xff)

                if (colOffset < widthMax) {
                    inPixelIndex++
                    colOffset++
                }

                val pixel = imagePixels[inPixelIndex]
                blurStack[stackIndex] = pixel

                red = ((pixel ushr 16) and 0xff)
                green = ((pixel ushr 8) and 0xff)
                blue = (pixel and 0xff)

                sumInputRed += red
                sumInputGreen += green
                sumInputBlue += blue

                sumRed += sumInputRed
                sumGreen += sumInputGreen
                sumBlue += sumInputBlue

                if (++stackPointer >= divisor) stackPointer = 0
                stackIndex = stackPointer

                blurStack[stackIndex].let { stackPixel ->
                            red = ((stackPixel ushr 16) and 0xff)
                    green = ((stackPixel ushr 8) and 0xff)
                    blue = (stackPixel and 0xff)

                    sumOutputRed += red
                    sumOutputGreen += green
                    sumOutputBlue += blue

                    sumInputRed -= red
                    sumInputGreen -= green
                    sumInputBlue -= blue
                }

            }
        }
    }
}
}

private class ProcessingColumn(
private val sharedValues: SharedValues,
private val imagePixels: IntArray,
private val bitmapSize: Size,
private val startColumn: Int,
private val endColumn: Int,
private val blurRadius: Int,
) {
operator fun invoke() {
    var xOffset: Int
    var yOffset: Int
    var blurStackIndex: Int
    var stackStart: Int
    var stackIndex: Int
    var stackPointer: Int

    var sourceIndex: Int
    var destinationIndex: Int

    var sumRed: Long
    var sumGreen: Long
    var sumBlue: Long
    var sumInputRed: Long
    var sumInputGreen: Long
    var sumInputBlue: Long
    var sumOutputRed: Long
    var sumOutputGreen: Long
    var sumOutputBlue: Long

    var red: Int
    var green: Int
    var blue: Int

    sharedValues.run {
        val blurStack = IntArray(divisor)

        for (col in startColumn..endColumn) {
            sumOutputBlue = 0
            sumOutputGreen = 0
            sumOutputRed = 0

            sumInputBlue = 0
            sumInputGreen = 0
            sumInputRed = 0

            sumBlue = 0
            sumGreen = 0
            sumRed = 0

            sourceIndex = col

            for (rad in 0..blurRadius) {
                stackIndex = rad
                var pixel = imagePixels[sourceIndex]
                blurStack[stackIndex] = pixel

                red = ((pixel ushr 16) and 0xff)
                green = ((pixel ushr 8) and 0xff)
                blue = (pixel and 0xff)

                var multiplier = rad + 1

                sumRed += red * multiplier
                sumGreen += green * multiplier
                sumBlue += blue * multiplier

                sumOutputRed += red
                sumOutputGreen += green
                sumOutputBlue += blue

                if (rad >= 1) {
                    if (rad <= heightMax) sourceIndex += bitmapSize.width

                    stackIndex = rad + blurRadius
                    pixel = imagePixels[sourceIndex]
                    blurStack[stackIndex] = pixel

                    multiplier = blurRadius + 1 - rad

                    red = ((pixel ushr 16) and 0xff)
                    green = ((pixel ushr 8) and 0xff)
                    blue = (pixel and 0xff)

                    sumRed += red * multiplier
                    sumGreen += green * multiplier
                    sumBlue += blue * multiplier

                    sumInputRed += red
                    sumInputGreen += green
                    sumInputBlue += blue
                }
            }

            stackPointer = blurRadius
            yOffset = minOf(blurRadius, heightMax)
            sourceIndex = col + yOffset * bitmapSize.width
            destinationIndex = col

            for (y in 0 until bitmapSize.height) {
                imagePixels[destinationIndex] =
                        ((imagePixels[destinationIndex] and 0xff000000.toInt()) or ((((sumRed.toInt() * multiplySum) ushr shiftSum) and 0xff) shl 16) or ((((sumGreen.toInt() * multiplySum) ushr shiftSum.toInt()) and 0xff) shl 8) or (((sumBlue.toInt() * multiplySum) ushr shiftSum.toInt()) and 0xff))

                destinationIndex += bitmapSize.width
                sumRed -= sumOutputRed
                sumGreen -= sumOutputGreen
                sumBlue -= sumOutputBlue

                stackIndex = (stackPointer + divisor - blurRadius).let {
                    stackStart = if (it >= divisor) it - divisor else it
                                stackStart
                }

                sumOutputRed -= ((blurStack[stackIndex] ushr 16) and 0xff)
                sumOutputGreen -= ((blurStack[stackIndex] ushr 8) and 0xff)
                sumOutputBlue -= (blurStack[stackIndex] and 0xff)

                if (yOffset < heightMax) {
                    sourceIndex += bitmapSize.width
                    yOffset++
                }

                blurStack[stackIndex] = imagePixels[sourceIndex]

                sumInputRed += ((imagePixels[sourceIndex] ushr 16) and 0xff)
                sumInputGreen += ((imagePixels[sourceIndex] ushr 8) and 0xff)
                sumInputBlue += (imagePixels[sourceIndex] and 0xff)

                sumRed += sumInputRed
                sumGreen += sumInputGreen
                sumBlue += sumInputBlue

                if (++stackPointer >= divisor) stackPointer = 0
                stackIndex = stackPointer

                blurStack[stackIndex].let { stackPixel ->
                            red = ((stackPixel ushr 16) and 0xff)
                    green = ((stackPixel ushr 8) and 0xff)
                    blue = (stackPixel and 0xff)

                    sumOutputRed += red
                    sumOutputGreen += green
                    sumOutputBlue += blue

                    sumInputRed -= red
                    sumInputGreen -= green
                    sumInputBlue -= blue
                }
            }
        }
    }
}
}

}
*/