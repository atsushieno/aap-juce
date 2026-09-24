package org.androidaudioplugin.juce;

import android.content.Context;
import android.util.Log;
import org.androidaudioplugin.AudioPluginService;
import org.androidaudioplugin.AudioPluginServiceHelper;
import org.androidaudioplugin.PluginInformation;
import org.androidaudioplugin.PluginServiceInformation;
import java.util.LinkedHashSet;
import java.util.Set;

/**
 * Initializes JUCE in the process of the AudioPluginService that declares this class in its
 * `org.androidaudioplugin.AudioPluginService.V4#Extensions` meta-data.
 *
 * It is an alternative to JuceAppInitializer (androidx.startup), which runs only in the main
 * process of the application. A package that contains more than one JUCE plugin has to run them
 * in separate processes (one JUCE runtime per process; see "Running plugins in separate processes"
 * in aap-core docs/DEVELOPERS.md), and declares this extension on each AudioPluginService.
 *
 * It loads the plugin library of the service (the `library` attribute in aap_metadata.xml) and
 * calls com.rmsl.juce.Java.initialiseJUCE(). Therefore com.rmsl.juce.Java must not load a
 * library in its static initializer.
 */
public class JuceAudioPluginServiceExtension implements AudioPluginService.Extension {
    private static final String LOG_TAG = "AAP-JUCE";

    // The service may be destroyed and created again within the same process, but JUCE is
    // initialized only once per process.
    private static boolean initialized = false;

    @Override
    public void initialize(Context context) {
        synchronized (JuceAudioPluginServiceExtension.class) {
            if (initialized)
                return;

            // the context is the AudioPluginService that declares this extension.
            String serviceClassName = context.getClass().getName();
            PluginServiceInformation service =
                    AudioPluginServiceHelper.getLocalAudioPluginService(context, serviceClassName);
            Set<String> libraries = new LinkedHashSet<>();
            for (PluginInformation plugin : service.getPlugins())
                if (plugin.getSharedLibraryName() != null)
                    libraries.add(plugin.getSharedLibraryName());

            if (libraries.isEmpty()) {
                Log.e(LOG_TAG, serviceClassName + " hosts no plugin. JUCE is not initialized.");
                return;
            }
            if (libraries.size() > 1)
                throw new IllegalStateException(serviceClassName + " hosts plugins from more than one library " +
                        libraries + ". Each JUCE plugin library has its own JUCE runtime, and they cannot share a process. " +
                        "Run them in separate AudioPluginServices (android:process), each with its own aap_metadata.xml.");

            String library = libraries.iterator().next();
            Log.i(LOG_TAG, "Initializing JUCE for " + library + " in " + serviceClassName);
            System.loadLibrary(toLoadLibraryName(library));
            com.rmsl.juce.Java.initialiseJUCE(context.getApplicationContext());
            initialized = true;
        }
    }

    @Override
    public void cleanup() {
        // JUCE lives as long as the process does.
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
