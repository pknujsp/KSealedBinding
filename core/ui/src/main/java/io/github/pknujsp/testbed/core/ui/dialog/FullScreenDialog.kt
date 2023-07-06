package io.github.pknujsp.testbed.core.ui.dialog

import android.app.Dialog
import io.github.pknujsp.blur.view.GLSurfaceLifeCycleListener

class FullScreenDialog(
  dialog: Dialog, attributes: SimpleDialogGeneralAttributes,
  styleAttributes: SimpleDialogStyleAttributes,
  blurringViewLifeCycleListener: GLSurfaceLifeCycleListener? = null,
) : SimpleDialog(dialog, attributes, styleAttributes, blurringViewLifeCycleListener) {


  override fun initTouchEvent() {}
}
