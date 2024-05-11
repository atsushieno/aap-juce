package org.androidaudioplugin.juce

import android.content.Context
import android.view.View
import android.view.ViewGroup
import android.widget.LinearLayout
import org.androidaudioplugin.AudioPluginServiceHelper
import org.androidaudioplugin.AudioPluginViewFactory
import org.androidaudioplugin.NativePluginService

class JuceAudioPluginViewFactory : AudioPluginViewFactory() {
    override fun createView(context: Context, pluginId: String, instanceId: Int): View =
        JuceAudioProcessorEditorView(context, pluginId, instanceId)
}

internal class JuceAudioProcessorEditorView(
    context: Context,
    private val pluginId: String,
    private val instanceId: Int) : LinearLayout(context) {

    companion object {
        @JvmStatic
        private external fun addAndroidComponentPeerViewTo(serviceInstance: Long, pluginId: String, instanceId: Int, juceAudioProcessorEditorView: JuceAudioProcessorEditorView)
    }

    constructor(context: Context) : this(context, "", 0)

    init {
        layoutParams = ViewGroup.LayoutParams(width, height)
        // adding the view is synchronously done.
        addAndroidComponentPeerViewTo(AudioPluginServiceHelper.getServiceInstance(pluginId), pluginId, instanceId, this)
    }
}