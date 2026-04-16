package org.androidaudioplugin.juce

import android.content.Context
import android.util.Size
import android.view.View
import android.view.ViewGroup
import android.widget.LinearLayout
import org.androidaudioplugin.AudioPluginServiceHelper
import org.androidaudioplugin.AudioPluginViewFactory

class JuceAudioPluginViewFactory : AudioPluginViewFactory() {
    override fun getPreferredSize(context: Context, pluginId: String, instanceId: Int): Size? {
        val values = getPreferredSize(AudioPluginServiceHelper.getServiceInstance(pluginId), pluginId, instanceId)
        if (values.size < 2 || values[0] <= 0 || values[1] <= 0)
            return null
        return Size(values[0], values[1])
    }

    override fun createView(context: Context, pluginId: String, instanceId: Int): View =
        JuceAudioProcessorEditorView(context, pluginId, instanceId)

    companion object {
        @JvmStatic
        private external fun getPreferredSize(serviceInstance: Long, pluginId: String, instanceId: Int): IntArray
    }
}

internal class JuceAudioProcessorEditorView(
    context: Context,
    private val pluginId: String,
    private val instanceId: Int) : LinearLayout(context) {

    companion object {
        // note: adding the view might be asynchronously done (depending on whether it is main thread or not).
        @JvmStatic
        private external fun addAndroidComponentPeerViewTo(serviceInstance: Long, pluginId: String, instanceId: Int, juceAudioProcessorEditorView: JuceAudioProcessorEditorView)
    }

    constructor(context: Context) : this(context, "", 0)

    init {
        layoutParams = ViewGroup.LayoutParams(200, 200) // stub values
        addAndroidComponentPeerViewTo(AudioPluginServiceHelper.getServiceInstance(pluginId), pluginId, instanceId, this)
    }
}
