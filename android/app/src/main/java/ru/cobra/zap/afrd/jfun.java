/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

package ru.cobra.zap.afrd;

import android.content.Context;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.Arrays;

/**
 * Here's where shit hits the fun.
 * Java has a remarkably wretched functionality when it comes to low-level
 * data types such as byte or C strings. This crippled class is meant to fill
 * some gaps in this horrible java world.
 */
class jfun
{
    /**
     * Compare two C strings until first 0 is encountered in either of the strings.
     * Returns 0 if strings are equal, or a positive value if first different
     * value in str1 is larger than same value in str2, or a negative value.
     *
     * @param arr1 The byte array containing first string
     * @param ofs1 the offset of first string within first byte array
     * @param arr2 The byte array containing second string
     * @param ofs2 the offset of second string within second byte array
     * @return 0 if strings are equal, or a positive or negative value.
     */
    static int strcmp (byte [] arr1, int ofs1, byte [] arr2, int ofs2)
    {
        while (ofs1 < arr1.length && ofs2 < arr2.length)
        {
            int b1 = arr1 [ofs1++] & 0xff;
            int b2 = arr2 [ofs2++] & 0xff;
            int diff = b1 - b2;
            if (b1 == 0 || b2 == 0)
                return diff;
            if (diff != 0)
                return diff;
        }
        return 0;
    }

    static String cstr (byte[] data)
    {
        return cstr (data, 0);
    }

    static String cstr (byte[] data, int ofs)
    {
        int cur = ofs;
        while (cur < data.length && data [cur] != '\0')
            cur++;

        // String(byte[], int, int) is deprecated in API27
        // and StringBuilder does not support byte[] - what a mess
        return new String (Arrays.copyOfRange (data, ofs, cur));
    }

    static String cstr (byte[] data, int ofs, int maxlen)
    {
        int cur = ofs;
        int end = ofs + maxlen;
        while (cur < end && data [cur] != '\0')
            cur++;

        // String(byte[], int, int) is deprecated in API27
        // and StringBuilder does not support byte[] - what a mess
        return new String (Arrays.copyOfRange (data, ofs, cur));
    }

    static void logExc (String func, Exception exc)
    {
        Log.e ("afrd", String.format ("Exception in %s: %s", func, exc.getMessage ()));
    }

    static boolean extractFile (Context ctx, int res_id, File outf)
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
        catch (IOException exc)
        {
            logExc ("extractFile", exc);
        }

        return false;
    }
}
