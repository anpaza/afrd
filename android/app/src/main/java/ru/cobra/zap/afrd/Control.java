package ru.cobra.zap.afrd;

import android.content.Context;
import android.os.Build;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.List;

import eu.chainfire.libsuperuser.Shell;
import ru.cobra.zap.afrdctl.R;

/**
 * This class provides a low-level API for communicating with afrd,
 */
public class Control
{
    private static final String AFRD_PID_FILE = "/dev/run/afrd.pid";
    // this is also present on Android 8 & 9, so check for that first
    private static final String SYSFS_ANDROID67 = "/sys/class/switch/hdmi/state";
    // this is present only on Android 8 & 9
    private static final String SYSFS_ANDROID89 = "/sys/class/switch/hdmi/cable.0/state";

    public File mIni;
    public File mAfrd;

    public Control (Context ctx)
    {
        mIni = new File (ctx.getCacheDir (), "afrd.ini");
        mAfrd = new File (ctx.getCacheDir (), "afrd");
    }

    public void restart ()
    {
        mAfrd.setExecutable (true);
        String[] cmd = new String [] { mAfrd.getPath () + " -k -D " + mIni.getPath () };
        Shell.run ("su", cmd, null, false);
    }

    public boolean isRunning ()
    {
        try
        {
            FileInputStream fis = new FileInputStream (AFRD_PID_FILE);
            byte[] data = new byte [300];
            int n = fis.read (data);
            fis.close ();

            while ((n > 0) && ((data [n - 1] < (byte)'0') || (data [n - 1] > (byte)'9')))
                n--;

            if (n > 0)
            {
                int pid = Integer.parseInt (new String (data, 0, n));
                fis = new FileInputStream ("/proc/" + pid + "/cmdline");
                n = fis.read (data);
                fis.close ();

                if (javafun.strcmp (mAfrd.getPath ().getBytes (), 0, data, 0) == 0)
                    return true;
            }
        }
        catch (Exception ignored)
        {
        }

        return false;
    }

    private boolean extractFile (Context ctx, int res_id, File outf)
    {
        try
        {
            InputStream is = ctx.getResources ().openRawResource (res_id);
            FileOutputStream fs = new FileOutputStream (outf);

            byte[] buff = new byte[4096];
            int n;
            while ((n = is.read (buff)) != -1)
                fs.write (buff, 0, n);

            fs.close ();
            is.close ();

            return true;
        }
        catch (IOException ignored)
        {
        }

        return false;
    }

    public boolean extractConfig (Context ctx)
    {
        int res_id;
        if (new File (SYSFS_ANDROID89).exists ())
        {
            // Android 8 or 9
            res_id = R.raw.afrd_8;
        }
        else if (new File (SYSFS_ANDROID67).exists ())
        {
            // Android 6 or 7
            res_id = R.raw.afrd_7;
        }
        else
        {
            return false;
        }

        if (!extractFile (ctx, res_id, mIni))
            return false;

        return true;
    }

    public boolean extractDaemon (Context ctx)
    {
        for (String arch : Build.SUPPORTED_ABIS)
        {
            int res_id;
            switch (arch)
            {
                case "armeabi-v7a":
                    res_id = R.raw.afrd_armeabi_v7a;
                    break;

                case "arm64-v8a":
                    res_id = R.raw.afrd_arm64_v8a;
                    break;

                default:
                    continue;
            }

            return extractFile (ctx, res_id, mAfrd);
        }

        return false;
    }

    /**
     * Touch config file so that afrd reloads it in a few seconds.
     */
    public void reload ()
    {
        try
        {
            long now = System.currentTimeMillis ();
            if (!mIni.exists ())
                new FileOutputStream (mIni).close ();
            else
                mIni.setLastModified (now);
        }
        catch (IOException e)
        {
        }
    }
}