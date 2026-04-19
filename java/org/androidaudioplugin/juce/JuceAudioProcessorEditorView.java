package org.androidaudioplugin.juce;

import android.content.Context;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import org.androidaudioplugin.AudioPluginServiceHelper;

class JuceAudioProcessorEditorView extends LinearLayout {
    // note: adding the view might be asynchronously done (depending on whether it is main thread or not).
    private static native void addAndroidComponentPeerViewTo(long serviceInstance, String pluginId, int instanceId, JuceAudioProcessorEditorView view);

    private final String pluginId;
    private final int instanceId;

    JuceAudioProcessorEditorView(Context context) {
        this(context, "", 0);
    }

    JuceAudioProcessorEditorView(Context context, String pluginId, int instanceId) {
        super(context);
        this.pluginId = pluginId;
        this.instanceId = instanceId;
        setLayoutParams(new ViewGroup.LayoutParams(200, 200)); // stub values
        addAndroidComponentPeerViewTo(AudioPluginServiceHelper.getServiceInstance(pluginId), pluginId, instanceId, this);
    }
}
