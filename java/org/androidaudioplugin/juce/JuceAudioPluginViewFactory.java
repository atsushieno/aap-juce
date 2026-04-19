package org.androidaudioplugin.juce;

import android.content.Context;
import android.util.Size;
import android.view.View;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import org.androidaudioplugin.AudioPluginServiceHelper;
import org.androidaudioplugin.AudioPluginViewFactory;

public class JuceAudioPluginViewFactory extends AudioPluginViewFactory {
    private static native int[] getPreferredSize(long serviceInstance, String pluginId, int instanceId);

    @Override
    public Size getPreferredSize(Context context, String pluginId, int instanceId) {
        int[] values = getPreferredSize(AudioPluginServiceHelper.getServiceInstance(pluginId), pluginId, instanceId);
        if (values.length < 2 || values[0] <= 0 || values[1] <= 0)
            return null;
        return new Size(values[0], values[1]);
    }

    @Override
    public View createView(Context context, String pluginId, int instanceId) {
        return new JuceAudioProcessorEditorView(context, pluginId, instanceId);
    }
}
