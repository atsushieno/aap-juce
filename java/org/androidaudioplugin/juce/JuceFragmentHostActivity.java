package org.androidaudioplugin.juce;

import android.app.Activity;

/**
 * Hosts JUCE's FragmentOverlay (FileChooser, RuntimePermissions) in a plugin process that a host app
 * loads, where JUCE has no Activity of its own.
 *
 * JUCE starts it in a new task when it opens an overlay without an Activity, shows the overlay in it
 * once it is resumed, and finishes it when the overlay is closed (see the "activity" fix in
 * cmake/JucePatchSets.cmake). Declare it in the plugin's AndroidManifest.xml:
 *
 * <pre>
 * &lt;activity android:name="org.androidaudioplugin.juce.JuceFragmentHostActivity"
 *     android:theme="@android:style/Theme.Translucent.NoTitleBar"
 *     android:configChanges="keyboard|keyboardHidden|orientation|screenLayout|screenSize|smallestScreenSize|uiMode"
 *     android:excludeFromRecents="true"
 *     android:exported="false"
 *     android:taskAffinity="" /&gt;
 * </pre>
 *
 * Android 14 and later allow a plugin process to start it only if the visible host binds the plugin
 * with Context.BIND_ALLOW_ACTIVITY_STARTS.
 */
public class JuceFragmentHostActivity extends Activity {
    // The tag that JUCE's FragmentOverlay::open() shows the overlay with.
    private static final String FRAGMENT_OVERLAY_TAG = "FragmentOverlay";

    @Override
    @SuppressWarnings("deprecation") // JUCE uses android.app.DialogFragment.
    protected void onResume() {
        // JUCE shows its overlay from Application.ActivityLifecycleCallbacks.onActivityResumed() here.
        super.onResume();

        // Finish unless there is an overlay: JUCE has closed it (e.g. after the file chooser returned),
        // or it gave up waiting for this activity to come up.
        getFragmentManager().executePendingTransactions();
        if (getFragmentManager().findFragmentByTag(FRAGMENT_OVERLAY_TAG) == null)
            finish();
    }
}
