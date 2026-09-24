package org.androidaudioplugin.juce;

import android.content.Context;
import android.util.Log;
import androidx.startup.Initializer;
import org.androidaudioplugin.PluginInformation;
import org.androidaudioplugin.PluginServiceInformation;
import org.androidaudioplugin.hosting.AudioPluginHostHelper;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

/**
 * Initializes JUCE for each plugin library in this package (the `library` attribute in aap_metadata.xml).
 *
 * Each JUCE plugin library has its own JUCE runtime, and more than one of them can live in the same
 * process (as they do in desktop plugin hosts) as long as the app does not compile the JUCE Java
 * classes that have native methods (see "More Than One JUCE Plugin Library in an App" in
 * docs/JUCE_GUI_SUPPORT.md).
 *
 * The JNI_OnLoad() of each JUCE library binds com.rmsl.juce.Java.initialiseJUCE() to its own
 * implementation, replacing the previous binding. Therefore we load each library and immediately
 * initialize JUCE for it, one by one. It also means that com.rmsl.juce.Java must not load any other
 * library in its static initializer.
 */
public class JuceAppInitializer implements Initializer<Object> {
    private static final String LOG_TAG = "AAP-JUCE";

    @Override public Object create(Context context) {
        Set<String> libraries = new LinkedHashSet<>();
        for (PluginServiceInformation service : AudioPluginHostHelper.queryAudioPluginServices(context, context.getPackageName()))
            for (PluginInformation plugin : service.getPlugins())
                if (plugin.getSharedLibraryName() != null)
                    libraries.add(plugin.getSharedLibraryName());

        boolean initialized = false;
        for (String library : libraries) {
            try {
                System.loadLibrary(toLoadLibraryName(library));
            } catch (UnsatisfiedLinkError e) {
                // Only the plugins in this library will fail to instantiate.
                Log.e(LOG_TAG, "Failed to load " + library + " specified in aap_metadata.xml", e);
                continue;
            }
            com.rmsl.juce.Java.initialiseJUCE(context.getApplicationContext());
            initialized = true;
        }
        if (!initialized)
            // No plugin library in this package (e.g. a host app). com.rmsl.juce.Java is supposed
            // to load the JUCE library by itself.
            com.rmsl.juce.Java.initialiseJUCE(context.getApplicationContext());
        return new Object();
    }

    @Override
    public List<Class<? extends Initializer<?>>> dependencies() {
        return new ArrayList();
    }

    // "libFoo.so" -> "Foo", which is what System.loadLibrary() expects.
    static String toLoadLibraryName(String fileName) {
        String name = fileName;
        if (name.startsWith("lib"))
            name = name.substring(3);
        if (name.endsWith(".so"))
            name = name.substring(0, name.length() - 3);
        return name;
    }
}
