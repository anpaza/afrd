package ru.cobra.zap.afrd.gui;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;

import ru.cobra.zap.afrd.Config;
import ru.cobra.zap.afrd.Control;

public class ActionActivity extends Activity
{
    static final String INTENT_START = "ru.cobra.afrd.START";
    static final String INTENT_STOP = "ru.cobra.afrd.STOP";

    @Override
    protected void onCreate (Bundle savedInstanceState)
    {
        super.onCreate (savedInstanceState);

        Intent intent = getIntent ();
        String action = intent.getAction ();

        if (action != null)
            if (action.equals (INTENT_START))
                enableAfrd (true);
            else if (action.equals (INTENT_STOP))
                enableAfrd (false);

        finishAndRemoveTask ();
    }

    private void enableAfrd (boolean state)
    {
        Control control = new Control (this);
        Config config = new Config ();
        if (!config.load (control.mIni))
            return;

        if (state == config.getBoolean ("enable", true))
            return;

        config.put ("enable", state);
        config.save (control.mIni);
    }
}
