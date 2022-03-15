package org.androidaudioplugin.juce;

import android.content.Context;
import androidx.startup.Initializer;
import java.util.ArrayList;
import java.util.List;

public class JuceAppInitializer implements Initializer<Object> {
    @Override public Object create(Context context) {
        com.rmsl.juce.Java.initialiseJUCE(context.getApplicationContext());
        return new Object();
    }

    @Override
    public List<Class<? extends Initializer<?>>> dependencies() {
        return new ArrayList();
    }
}


