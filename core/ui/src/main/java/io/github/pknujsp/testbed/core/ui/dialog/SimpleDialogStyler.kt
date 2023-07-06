package io.github.pknujsp.testbed.core.ui.dialog

import android.app.Activity
import android.app.Dialog
import android.content.Context
import android.content.res.Resources
import android.graphics.drawable.Drawable
import android.graphics.drawable.LayerDrawable
import android.os.Build
import android.util.LruCache
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.Window
import android.view.WindowManager
import android.view.WindowManager.LayoutParams
import android.widget.FrameLayout
import android.widget.LinearLayout
import androidx.annotation.ColorInt
import androidx.annotation.DrawableRes
import androidx.core.content.res.ResourcesCompat
import androidx.core.view.allViews
import androidx.core.view.children
import androidx.core.view.updateLayoutParams
import androidx.core.view.updateMarginsRelative
import io.github.pknujsp.blur.view.BlurringView
import io.github.pknujsp.blur.view.GLSurfaceLifeCycleListener
import io.github.pknujsp.blur.view.IGLSurfaceViewLayout
import io.github.pknujsp.testbed.core.ui.R


internal class SimpleDialogStyler(
  val simpleDialogStyleAttributes: SimpleDialogStyleAttributes,
  context: Context,
) {
  private val activityContextWindow = (context as Activity).window

  // android.R.id.content
  private var androidDefaultContentView: FrameLayout? = null

  // R.id.dialog_base_content
  private var compatContentView: FrameLayout? = null

  // R.id.blurring_view
  var blurringViewLifeCycleListener: GLSurfaceLifeCycleListener? = null
  var iBlurringViewLayout: IGLSurfaceViewLayout? = null

  private companion object {
    private val density: Float = Resources.getSystem().displayMetrics.density

    private val drawableCache = LruCache<BackgroundDrawableInfo, Drawable>(10)
  }

  private data class BackgroundDrawableInfo(
    val topStartCornerRadius: Float,
    val topEndCornerRadius: Float,
    val bottomStartCornerRadius: Float,
    val bottomEndCornerRadius: Float,
    val isShowModal: Boolean,
    @ColorInt val backgroundColor: Int,
    @DrawableRes val customBackgroundDrawableId: Int?,
  )

  fun applyStyle(dialog: Dialog) {
    dialog.window?.let { dialogWindow ->
      val decorView = dialogWindow.decorView as ViewGroup
      decorView.allViews.forEach { view ->
        if (view.id == R.id.dialog_base_content) compatContentView = view as FrameLayout
        else if (view.id == android.R.id.content) androidDefaultContentView = view as FrameLayout
        if (androidDefaultContentView != null && compatContentView != null) return@forEach
      }
      (decorView.children.first() as LinearLayout).gravity = Gravity.CENTER

      updateAndroidDefaultContentViewLayout()
      setCompatContentViewLayout()
      setBackgroundAndModal()

      dialogWindow.attributes.also { attr ->
        attr.copyFrom(attr)
        updateWindowLayout(attr)
        setBlur(decorView, dialogWindow, attr)
        setDim(dialogWindow, attr)
      }
    }
  }

  private fun setBlur(decorView: View, window: Window, attributes: LayoutParams) {
    if (!simpleDialogStyleAttributes.behindBlur && !simpleDialogStyleAttributes.backgroundBlur) return

    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
      val maxRadius = 30f * density

      if (simpleDialogStyleAttributes.backgroundBlur) window.setBackgroundBlurRadius((simpleDialogStyleAttributes.backgroundBlurIndensity / 100f * maxRadius).toInt())
      if (simpleDialogStyleAttributes.behindBlur) {
        window.addFlags(WindowManager.LayoutParams.FLAG_BLUR_BEHIND)
        attributes.blurBehindRadius = (simpleDialogStyleAttributes.behindBlurIndensity / 100f * maxRadius).toInt()
      }
    } else if (simpleDialogStyleAttributes.behindBlurForce) {
      BlurringView(activityContextWindow.context, (simpleDialogStyleAttributes.behindBlurIndensity / 100f * 25f).toInt()).also {
        androidDefaultContentView?.addView(it, 0, it.layoutParams)
        blurringViewLifeCycleListener = it
        iBlurringViewLayout = it
      }
    }
  }

  private fun setDim(window: Window, attributes: WindowManager.LayoutParams) {
    if (simpleDialogStyleAttributes.dim) {
      iBlurringViewLayout?.apply { setBackgroundColor(simpleDialogStyleAttributes.dimIndensity shl 24) } ?: run {
        window.addFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND)
        attributes.dimAmount = simpleDialogStyleAttributes.dimIndensity / 100f
      }
    }
  }


  private fun setBackgroundAndModal() {
    compatContentView?.apply {
      val backgroundDrawableInfo = BackgroundDrawableInfo(
        topStartCornerRadius = simpleDialogStyleAttributes.topStartCornerRadius * density,
        topEndCornerRadius = simpleDialogStyleAttributes.topEndCornerRadius * density,
        bottomStartCornerRadius = simpleDialogStyleAttributes.bottomStartCornerRadius * density,
        bottomEndCornerRadius = simpleDialogStyleAttributes.bottomEndCornerRadius * density,
        isShowModal = simpleDialogStyleAttributes.isShowModalPoint,
        backgroundColor = simpleDialogStyleAttributes.backgroundColor,
        customBackgroundDrawableId = simpleDialogStyleAttributes.backgroundResourceId,
      )

      drawableCache.get(backgroundDrawableInfo)?.let { drawable ->
        background = drawable
      } ?: run {
        if (backgroundDrawableInfo.customBackgroundDrawableId == null) {
          val drawables = mutableListOf<Drawable>()

          drawables.add(
            CornersDrawable(
              color = backgroundDrawableInfo.backgroundColor,
              topStartRadius = backgroundDrawableInfo.topStartCornerRadius,
              topEndRadius = backgroundDrawableInfo.topEndCornerRadius,
              bottomStartRadius = backgroundDrawableInfo.bottomStartCornerRadius,
              bottomEndRadius = backgroundDrawableInfo.bottomEndCornerRadius,
            ).apply {
              this.setBounds(0, 0, width, height)
            },
          )

          var iconHeight = 0
          if (simpleDialogStyleAttributes.isShowModalPoint) {
            iconHeight = (12 * density).toInt()
            val icon = ResourcesCompat.getDrawable(
              context.resources,
              simpleDialogStyleAttributes.customModalViewId ?: R.drawable.icon_more_edited,
              null,
            )
            if (icon != null) {
              icon.setBounds(0, 0, iconHeight, iconHeight)
              drawables.add(icon)
            }
          }

          background = LayerDrawable(drawables.toTypedArray()).apply {
            if (simpleDialogStyleAttributes.isShowModalPoint) {
              setLayerGravity(1, Gravity.CENTER_HORIZONTAL or Gravity.TOP)
              setLayerInsetTop(0, iconHeight)
            }
            drawableCache.put(backgroundDrawableInfo, this)
          }
        } else {
          setBackgroundResource(backgroundDrawableInfo.customBackgroundDrawableId)
        }
      }
    }
  }

  private fun updateAndroidDefaultContentViewLayout() {
    androidDefaultContentView?.apply {
      updateLayoutParams<ViewGroup.MarginLayoutParams> {
        width = ViewGroup.LayoutParams.MATCH_PARENT
        height = ViewGroup.LayoutParams.MATCH_PARENT
      }
      updateLayoutParams<LinearLayout.LayoutParams> {
        gravity = Gravity.CENTER
      }
    }
  }

  private fun setCompatContentViewLayout() {
    compatContentView?.apply {
      updateLayoutParams<ViewGroup.MarginLayoutParams> {
        updateMarginsRelative(
          start = simpleDialogStyleAttributes.startMargin * density.toInt(),
          top = simpleDialogStyleAttributes.topMargin * density.toInt(),
          end = simpleDialogStyleAttributes.endMargin * density.toInt(),
          bottom = simpleDialogStyleAttributes.bottomMargin * density.toInt(),
        )

        width = ViewGroup.LayoutParams.WRAP_CONTENT
        height = ViewGroup.LayoutParams.WRAP_CONTENT
      }
    }
  }

  private fun updateWindowLayout(attributes: WindowManager.LayoutParams) {
    attributes.apply {
      width = ViewGroup.LayoutParams.MATCH_PARENT
      height = ViewGroup.LayoutParams.MATCH_PARENT
    }
  }

  private fun setElevation() {
    if (simpleDialogStyleAttributes.elevation > 0) {
      compatContentView?.elevation = simpleDialogStyleAttributes.elevation * density
    }
  }

}
