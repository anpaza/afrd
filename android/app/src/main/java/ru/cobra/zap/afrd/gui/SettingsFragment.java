/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

package ru.cobra.zap.afrd.gui;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.preference.EditTextPreference;
import android.preference.ListPreference;
import android.preference.MultiSelectListPreference;
import android.preference.Preference;
import android.preference.PreferenceFragment;
import android.preference.PreferenceGroup;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;

/**
 * This fragment implements a editor for AFRd settings.
 */
public class SettingsFragment extends PreferenceFragment
    implements SharedPreferences.OnSharedPreferenceChangeListener
{
    private MainActivity mMain;

    static SettingsFragment create ()
    {
        return new SettingsFragment ();
    }

    @Override
    public void onCreate (Bundle savedInstanceState)
    {
        super.onCreate (savedInstanceState);

        getPreferenceManager ().setSharedPreferencesName ("ini_options");

        // Load the preferences from an XML resource
        for (int t = 0; t < 2; t++)
            try
            {
                addPreferencesFromResource (R.xml.preferences);
                break;
            }
            catch (Exception ignored)
            {
                mMain.resetPrefs ();
            }
    }

    @Override
    public View onCreateView (LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState)
    {
        View root = inflater.inflate (R.layout.fragment_prefs, container, false);

        View b = root.findViewById (R.id.buttonPrefsReset);
        if (b != null)
            b.setOnClickListener (new View.OnClickListener ()
            {
                @Override
                public void onClick (View v)
                {
                    mMain.resetPrefs ();
                }
            });

        return root;
    }

    @Override
    public void onResume ()
    {
        super.onResume ();

        getPreferenceManager ().getSharedPreferences ()
            .registerOnSharedPreferenceChangeListener (this);
    }

    @Override
    public void onPause ()
    {
        super.onPause ();

        getPreferenceScreen ().getSharedPreferences ()
            .unregisterOnSharedPreferenceChangeListener (this);
    }

    @Override
    public void onAttach (Context context)
    {
        super.onAttach (context);
        if (context instanceof MainActivity)
            mMain = (MainActivity)context;
    }

    @Override
    public void onDetach ()
    {
        super.onDetach ();
        mMain = null;
    }

    @Override
    public void onSharedPreferenceChanged (SharedPreferences sharedPreferences, String key)
    {
        if (mMain != null)
            mMain.applyPrefs ();
    };
}
