/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

package ru.cobra.zap.afrd.gui;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Fragment;
import android.app.FragmentManager;
import android.app.FragmentTransaction;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.res.Configuration;
import android.content.res.Resources;
import android.os.AsyncTask;
import android.os.Bundle;
import android.os.Handler;
import android.preference.Preference;
import android.preference.PreferenceFragment;
import android.preference.PreferenceManager;
import android.preference.PreferenceScreen;
import android.util.ArrayMap;
import android.util.DisplayMetrics;
import android.view.Display;
import android.view.View;
import android.view.WindowManager;
import android.widget.AdapterView;
import android.widget.ListView;
import android.widget.SimpleAdapter;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Vector;

import eu.chainfire.libsuperuser.Shell;
import ru.cobra.zap.afrd.Config;
import ru.cobra.zap.afrd.Control;

public class MainActivity extends Activity
    implements PreferenceFragment.OnPreferenceStartFragmentCallback
{
    PackageInfo mPackageInfo;
    Control mControl;
    SharedPreferences mOptions;
    Vector<PreferenceScreen> mPrefStack;
    // used for the "press Back again to quit" thing
    long mBackTime = 0;

    @Override
    protected void onCreate (Bundle savedInstanceState)
    {
        super.onCreate (savedInstanceState);

        limitDpi ();

        setContentView (R.layout.activity_main);

        mPrefStack = new Vector<> ();

        try
        {
            mPackageInfo = getPackageManager ().getPackageInfo (getPackageName (), 0);
        }
        catch (PackageManager.NameNotFoundException e)
        {
            // Can't find own package? Gross!
            finishAndRemoveTask ();
        }

        ((TextView) findViewById (R.id.version)).setText (
            String.format (getString (R.string.afrdctl_version),
                BuildConfig.VERSION_NAME, BuildConfig.BUILD_TYPE));

        mControl = new Control (this);
        mOptions = getSharedPreferences ("ini_options", 0);

        createMainMenu ();
        prepareConfig ();
        prepareDaemon ();
        prepareSuperuser ();

        addFragment (StatusFragment.create (), "status");
    }

    private void createMainMenu ()
    {
        List<Map<String, String>> data = new ArrayList<> ();
        Map<String, String> menuent;
        menuent = new ArrayMap<> (2);
        menuent.put ("item", getString (R.string.menu_status));
        menuent.put ("desc", getString (R.string.menu_desc_status));
        data.add (menuent);
        menuent = new ArrayMap<> (2);
        menuent.put ("item", getString (R.string.menu_settings));
        menuent.put ("desc", getString (R.string.menu_desc_settings));
        data.add (menuent);
        menuent = new ArrayMap<> (2);
        menuent.put ("item", getString (R.string.menu_log));
        menuent.put ("desc", getString (R.string.menu_desc_log));
        data.add (menuent);
        menuent = new ArrayMap<> (2);
        menuent.put ("item", getString (R.string.menu_faq));
        menuent.put ("desc", getString (R.string.menu_desc_faq));
        data.add (menuent);
        menuent = new ArrayMap<> (2);
        menuent.put ("item", getString (R.string.menu_about));
        menuent.put ("desc", getString (R.string.menu_desc_about));
        data.add (menuent);

        SimpleAdapter adapter = new SimpleAdapter (this, data, android.R.layout.simple_list_item_2,
            new String[] { "item", "desc" }, new int[] { android.R.id.text1, android.R.id.text2 });
        ListView list = findViewById (R.id.menu);
        list.setAdapter(adapter);
        list.setOnItemClickListener (new AdapterView.OnItemClickListener ()
        {
            @Override
            public void onItemClick (AdapterView<?> parent, View view, int position, long id)
            {
                switch ((int)id)
                {
                    case 0:
                        if (getFragment ("status") == null)
                            addFragment (StatusFragment.create (), "status");
                        break;

                    case 1:
                        if (getFragment ("settings") == null)
                            addFragment (SettingsFragment.create (), "settings");
                        break;

                    case 2:
                        if (getFragment ("log") == null)
                            addFragment (LogFragment.create (
                                mOptions.getString ("log_file", null),
                                mOptions.getBoolean ("log_enable", false)),
                                "log");
                        break;

                    case 3:
                        if (getFragment ("faq") == null)
                            addFragment (FAQFragment.create (), "faq");
                        break;

                    case 4:
                        if (getFragment ("about") == null)
                            addFragment (AboutFragment.create (), "about");
                        break;
                }
            }
        });

    }

    @SuppressWarnings( "deprecation" )
    public void limitDpi ()
    {
        Configuration configuration = getResources ().getConfiguration ();

        if ((configuration.densityDpi <= 240) &&
            (configuration.fontScale <= 1.30F))
            return;

        // Don't allow too large fonts otherwise it's all unusable & unreadable
        if (configuration.densityDpi > 340)
            configuration.densityDpi = 340;
        if (configuration.fontScale > 1.30F)
            configuration.fontScale = 1.30F;

        DisplayMetrics metrics = getResources ().getDisplayMetrics ();
        WindowManager wm = (WindowManager) getSystemService (WINDOW_SERVICE);
        if (wm == null)
            return;

        Display dpy = wm.getDefaultDisplay ();
        dpy.getMetrics (metrics);
        metrics.scaledDensity = configuration.fontScale * metrics.density;
        getBaseContext ().getResources ().updateConfiguration (configuration, metrics);
    }

    private boolean prepareConfig ()
    {
        SharedPreferences prefs = getSharedPreferences ("requisites", 0);
        boolean rewrite = true;

        if (mControl.mIni.exists ())
            try
            {
                rewrite = !prefs.getString ("afrd_ini_ver", "").equals (mPackageInfo.versionName);
            }
            catch (java.lang.ClassCastException ignored)
            {
            }

        if (rewrite)
        {
            String res = mControl.extractConfig (this);
            if (!res.isEmpty ())
            {
                showMessage (getString (R.string.msg_title_critical_error), res, null);
                return false;
            }

            prefs.edit ().putString ("afrd_ini_ver", mPackageInfo.versionName).apply ();
        }

        return updateConfig ();
    }

    private boolean updateConfig ()
    {
        // load config options from ini file
        Config conf = new Config ();
        if (!conf.load (mControl.mIni))
            return false;

        if (conf.getString ("log.file", null) == null)
            conf.put ("log.file", new File (getCacheDir (), "afrd.log").getPath ());

        // apply user preferences on top
        if (mOptions.contains ("enable"))
            conf.load (mOptions);
        else
        {
            PreferenceManager.setDefaultValues (this, "ini_options", 0, R.xml.preferences, true);
            conf.save (mOptions);
        }

        // and save the modified file
        return conf.condSave (mControl.mIni);
    }

    private void prepareDaemon ()
    {
        SharedPreferences prefs = getSharedPreferences ("requisites", 0);
        boolean rewrite = true;

        if (mControl.mAfrd.exists ())
            try
            {
                rewrite = !prefs.getString ("afrd_ver", "").equals (mPackageInfo.versionName);
            }
            catch (java.lang.ClassCastException ignored)
            {
            }

        if (!rewrite)
            return;

        String res = mControl.extractDaemon (this);
        if (!res.isEmpty ())
        {
            showMessage (getString (R.string.msg_title_critical_error), res, null);
            return;
        }

        prefs.edit ().putString ("afrd_ver", mPackageInfo.versionName).apply ();
    }

    private void prepareSuperuser ()
    {
        AsyncTask.execute (new Runnable ()
        {
            @Override
            public void run ()
            {
                if (!Shell.SU.available ())
                {
                    showToast (R.string.afrd_requires_su, true);
                    return;
                }

                startService (new Intent (getApplicationContext (), AFRService.class));
            }
        });
    }

    private void showMessage (String title, String msg, DialogInterface.OnClickListener onOk)
    {
        AlertDialog.Builder dlg = new AlertDialog.Builder (this);
        dlg.setMessage (msg);
        dlg.setTitle (title);
        dlg.setCancelable (true);
        dlg.setPositiveButton (getResources ().getString (R.string.ok), onOk);
        dlg.create ().show ();
    }

    private void showMessage (int title_id, int msg_id, Object... args)
    {
        Resources res = getResources ();
        String msg = res.getString (msg_id);
        if (args.length > 0)
            msg = String.format (msg, args);
        showMessage (res.getString (title_id), msg, null);
    }

    private static Handler mAppHandler = new Handler ();

    public void showToast (final String msg, final boolean durable)
    {
        mAppHandler.post (new Runnable ()
        {
            @Override
            public void run ()
            {
                Toast.makeText (getApplicationContext (), msg,
                    durable ? Toast.LENGTH_LONG : Toast.LENGTH_SHORT).show ();
            }
        });
    }

    public void showToast (int msg_id, boolean durable)
    {
        String msg = getResources ().getString (msg_id);
        showToast (msg, durable);
    }

    public void addFragment (Fragment fragment, String tag, boolean toplevel)
    {
        if (fragment == null)
            return;

        FragmentManager fm = getFragmentManager ();

        if (toplevel)
        {
            mPrefStack.clear ();

            if (fm.getBackStackEntryCount () > 0)
                fm.popBackStack (fm.getBackStackEntryAt (0).getId (),
                    FragmentManager.POP_BACK_STACK_INCLUSIVE);
        }

        fm.beginTransaction ().
            replace (R.id.content_holder, fragment, tag).
            setTransition (FragmentTransaction.TRANSIT_FRAGMENT_FADE).
            addToBackStack (null).
            commit ();
    }

    public void addFragment (Fragment fragment, String tag)
    {
        addFragment (fragment, tag, true);
    }

    public Fragment getFragment (String tag)
    {
        return getFragmentManager ().findFragmentByTag (tag);
    }

    @Override
    public boolean onPreferenceStartFragment (PreferenceFragment caller, Preference pref)
    {
        if (pref instanceof PreferenceScreen)
        {
            mPrefStack.add (caller.getPreferenceScreen ());
            caller.setPreferenceScreen ((PreferenceScreen) pref);
        }
        return false;
    }

    @Override
    public void onBackPressed ()
    {
        if (mPrefStack.size () > 0)
        {
            PreferenceFragment frag = (PreferenceFragment) getFragment ("settings");
            if (frag != null)
            {
                int last = mPrefStack.size () - 1;
                PreferenceScreen ps = mPrefStack.elementAt (last);
                mPrefStack.removeElementAt (last);
                frag.setPreferenceScreen (ps);
                return;
            }
        }

        long now = System.currentTimeMillis ();
        if ((mBackTime == 0) || (now - mBackTime > 1000))
        {
            mBackTime = now;
            showToast (R.string.press_back_again, false);
            return;
        }

        finishAndRemoveTask ();
    }

    public void applyPrefs ()
    {
        Config conf = new Config ();
        if (!conf.load (mControl.mIni))
        {
            showToast (R.string.msg_apply_prefs_failed, true);
            return;
        }

        // load SharedPreferences into the ini file
        conf.load (mOptions);
        if (!conf.isModified ())
            return;

        if (!conf.save (mControl.mIni))
        {
            showToast (R.string.msg_apply_prefs_failed, true);
            return;
        }

        showToast (R.string.msg_apply_prefs_ok, true);
    }

    public void resetPrefs ()
    {
        // reset application preferences
        mOptions.edit ().clear ().apply ();

        // delete ini file & re-extract ini file from raw resources
        if (!mControl.mIni.delete () ||
            !prepareConfig ())
        {
            showToast (R.string.msg_reset_prefs_failed, true);
            return;
        }

        showToast (R.string.msg_reset_prefs_ok, true);

        // reset preferences if it is the active fragment
        if (getFragment ("settings") != null)
            addFragment (new SettingsFragment (), "settings");
    }
}
