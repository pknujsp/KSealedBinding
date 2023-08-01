# KSealedBinding

**KSealedBinding** is an automatic generation tool for binding functions designed for sealed classes or interfaces in Kotlin. It eradicates the need for manual function creation and excessive use of 'when' and 'if' statements, greatly enhancing code efficiency and readability. **KSealedBinding** offers Kotlin programmers an optimized and streamlined experience when working with sealed classes or interfaces. (**KSealedBinding**은 Kotlin의 sealed 클래스나 인터페이스에 대한 바인딩 함수를 자동으로 생성하는 도구입니다. 이 도구는 수동 함수 생성과 'when'이나 'if'문의 과도한 사용이 필요 없게 만들어 코드 효율성과 가독성을 크게 향상시킵니다. **KSealedBinding**은 sealed 클래스나 인터페이스를 작업할 때 Kotlin 프로그래머들에게 최적화되고 간소화된 경험을 제공합니다.)

## How to use
> In module level build.gradle

* **Required**
   * Enable **KSP**
   * **KAPT** is not supported.

```gradle
plugins {
  id("com.google.devtools.ksp")
}

dependencies {
  ksp("io.github.pknujsp:ksealedbinding-compiler:1.0.0")
  implementation("io.github.pknujsp:ksealedbinding-annotation:1.0.0")
}
```

## Example

> Add @KBindFunc to a sealed interface or class.

**Base**

```kolin
@KBindFunc
sealed interface UiState<out T> {
  data class Success<out T>(val data: T) : UiState<T>
  data class Error(val exception: Throwable) : UiState<Nothing>
  object Loading : UiState<Nothing>
}
```

**Generated Binding**

```kotlin
public inline fun <T> UiState<T>.onError(block: (Throwable) -> Unit): UiState<T> {
  if (this is UiState.Error)
    block(exception)
  return this
}

public inline fun <T> UiState<T>.onLoading(block: () -> Unit): UiState<T> {
  if (this is UiState.Loading)
    block()
  return this
}

public inline fun <T> UiState<T>.onSuccess(block: (T) -> Unit): UiState<T> {
  if (this is UiState.Success)
    block(data)
  return this
}
```

**Using**

```kotlin
weatherInfo.onLoading {
        CircularProgressIndicator(modifier = Modifier.align(Alignment.CenterHorizontally))
        Spacer(modifier = Modifier.height(16.dp))
        Text(text = "날씨 정보를 불러오는 중입니다", style = TextStyle(color = Color.Black))
      }.onSuccess { weatherInfo ->
        CurrentWeatherScreen(weatherInfoViewModel)
      }.onError { throwable ->
        Text(text = throwable.message ?: "Error")
      }
```
