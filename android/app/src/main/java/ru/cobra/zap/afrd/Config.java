package ru.cobra.zap.afrd;

import android.content.SharedPreferences;
import android.util.ArrayMap;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.nio.charset.StandardCharsets;

public class Config
{
    private ArrayMap<String, Object> mItems;

    private boolean mModified = false;

    // Configuration item data type
    public static final int TYPE_MASK = 0x0000000F;
    public static final int TYPE_BOOL = 0x00000001;
    public static final int TYPE_INT  = 0x00000002;
    public static final int TYPE_STR  = 0x00000003;

    public static ArrayMap<String, Integer> itemType;

    static
    {
        itemType = new ArrayMap<> ();
        itemType.put ("enable", TYPE_BOOL);
        itemType.put ("log.enable", TYPE_BOOL);
        itemType.put ("log.file", TYPE_STR);
        itemType.put ("mode.prefer.exact", TYPE_BOOL);
        itemType.put ("mode.use.fract", TYPE_STR);
        itemType.put ("mode.blacklist.rates", TYPE_STR);
        itemType.put ("mode.extra", TYPE_STR);
        itemType.put ("cs.select", TYPE_STR);
        itemType.put ("switch.delay.on", TYPE_INT);
        itemType.put ("switch.delay.off", TYPE_INT);
        itemType.put ("switch.delay.retry", TYPE_INT);
        itemType.put ("switch.timeout", TYPE_INT);
        itemType.put ("switch.blackout", TYPE_INT);
    }

    public Object get (String key, Object defval)
    {
        if (mItems.containsKey (key))
            return mItems.get (key);

        return defval;
    }

    public boolean getBoolean (String key, boolean defval)
    {
        if (mItems.containsKey (key))
        {
            Object ret = mItems.get (key);
            if (ret instanceof Boolean)
                return (Boolean) ret;
            return parseBoolean (ret.toString ());
        }

        return defval;
    }

    public int getInt (String key, int defval)
    {
        if (mItems.containsKey (key))
        {
            Object ret = mItems.get (key);
            if (ret instanceof Integer)
                return (Integer) ret;
            return Integer.parseInt (ret.toString ());
        }

        return defval;
    }

    public String getString (String key, String defval)
    {
        if (mItems.containsKey (key))
        {
            Object ret = mItems.get (key);
            if (ret instanceof String)
                return (String) ret;
            return ret.toString ();
        }

        return defval;
    }

    public void put (String key, Object val)
    {
        if (!mModified && !equals (mItems.get (key), val))
            mModified = true;
        mItems.put (key, val);
    }

    private boolean equals (Object a, Object b)
    {
        if ((a == null) || (b == null))
            return a == b;

        String as = a.toString ();
        String bs = b.toString ();
        return as.equals (bs);
    }

    public boolean load (File ini)
    {
        mItems = new ArrayMap<> ();
        mModified = false;

        try
        {
            FileInputStream fis = new FileInputStream (ini);
            BufferedReader bufr = new BufferedReader (new InputStreamReader (fis, StandardCharsets.UTF_8));
            String line;
            while ((line = bufr.readLine ()) != null)
            {
                line = line.trim ();
                // skip empty lines & comments
                if (line.length () == 0 || line.charAt (0) == '#')
                    continue;
                // otherwise, split key=value
                String key, val;
                int eq = line.indexOf ('=');
                if (eq < 0)
                {
                    // consider it a wrongly formed "key=" line
                    key = line;
                    val = "";
                }
                else
                {
                    key = line.substring (0, eq).trim ();
                    val = line.substring (eq + 1).trim ();
                }

                Object oval = val;
                Integer it = itemType.get (key);
                if (it != null)
                    switch (it & TYPE_MASK)
                    {
                        case TYPE_BOOL: oval = parseBoolean (val); break;
                        case TYPE_INT: oval = Integer.parseInt (val); break;
                        case TYPE_STR: break;
                    }

                mItems.put (key, oval);
            }
        }
        catch (IOException e)
        {
            return false;
        }

        return true;
    }

    private Boolean parseBoolean (String val)
    {
        val = val.trim ().toLowerCase ();
        return !("0".equals (val) || "false".equals (val) || "no".equals (val));
    }

    public boolean isModified ()
    { return mModified; }

    public boolean condSave (File ini)
    {
        if (!mModified)
            return true;

        return save (ini);
    }

    public boolean save (File ini)
    {
        File out_ini = new File (ini.getPath () + "~");
        try
        {
            FileInputStream fis = new FileInputStream (ini);
            FileOutputStream fos = new FileOutputStream (out_ini);
            BufferedReader bufr = new BufferedReader (new InputStreamReader (fis, StandardCharsets.UTF_8));
            BufferedWriter bufw = new BufferedWriter (new OutputStreamWriter (fos, StandardCharsets.UTF_8));
            String line;
            ArrayMap<String,Object> items_copy = new ArrayMap<> (mItems);
            while ((line = bufr.readLine ()) != null)
            {
                boolean disabled = false;
                line = line.trim ();

                // check for commented key=value pairs
                if (line.length () != 0 && line.charAt (0) == '#')
                {
                    int eq = line.indexOf ('=');
                    if (eq > 0)
                    {
                        String key = line.substring (1, eq).trim ();
                        if (items_copy.containsKey (key) ||
                            itemType.containsKey (key))
                        {
                            // it's a commented key=something
                            disabled = true;
                            line = line.substring (1);
                        }
                    }
                }

                // skip comments
                if (line.length () == 0 || line.charAt (0) == '#')
                {
                    bufw.write (line);
                }
                else
                {
                    // otherwise, split key=value
                    String key;
                    Object val;
                    int eq = line.indexOf ('=');
                    if (eq < 0)
                    {
                        // consider it a wrongly formed "key=" line
                        key = line;
                        val = "";
                    }
                    else
                    {
                        key = line.substring (0, eq);
                        val = line.substring (eq + 1);
                    }

                    if (items_copy.containsKey (key))
                    {
                        val = items_copy.get (key);
                        if (val instanceof Boolean)
                            val = ((Boolean) val) ? 1 : 0;

                        items_copy.remove (key);

                        disabled = (val == null);
                    }

                    if (disabled)
                        bufw.write ('#');
                    bufw.write (key);
                    bufw.write ("=");
                    bufw.write (val != null ? val.toString () : "");
                }
                bufw.newLine ();
            }

            // write the rest of items not encountered in config
            for (int i = 0; i < items_copy.size (); i++)
            {
                bufw.write (items_copy.keyAt (i));
                bufw.write ("=");
                Object val = items_copy.valueAt (i);
                bufw.write (val != null ? val.toString () : "");
                bufw.newLine ();
            }

            bufw.flush ();
        }
        catch (IOException e)
        {
            return false;
        }

        if (!ini.delete ())
            return false;

        if (!out_ini.renameTo (ini))
            return false;

        mModified = false;
        return true;
    }

    /**
     * Override ini values from user's application preferences
     *
     * @param prefs application preferences
     */
    public void load (SharedPreferences prefs)
    {
        for (String key : prefs.getAll ().keySet ())
        {
            String ini_key = key.replace ('_', '.');
            Integer it = itemType.get (ini_key);
            if (it != null)
                try
                {
                    Object val;
                    switch (it & TYPE_MASK)
                    {
                        case TYPE_BOOL: val = prefs.getBoolean (key, true); break;
                        case TYPE_INT: val = Integer.parseInt (prefs.getString (key, "0")); break;
                        case TYPE_STR: val = prefs.getString (key, ""); break;
                        default: continue;
                    }
                    put (ini_key, val);
                }
                catch (Exception ignored)
                {
                }
        }
    }

    /**
     * Save ini keys to user's application preferences.
     * This is usually called at first initialization to set default preference values.
     *
     * @param prefs application preferences
     */
    public void save (SharedPreferences prefs)
    {
        SharedPreferences.Editor edit = prefs.edit ();

        for (int i = 0; i < mItems.size (); i++)
        {
            String ini_key = mItems.keyAt (i);
            String key = ini_key.replace ('.', '_');
            Integer it = itemType.get (ini_key);
            if (it != null)
                switch (it & TYPE_MASK)
                {
                    case TYPE_BOOL: edit.putBoolean (key, getBoolean (ini_key, false)); break;
                    case TYPE_INT: edit.putString (key, ((Integer) getInt (ini_key, 0)).toString ()); break;
                    case TYPE_STR: edit.putString (key, getString (ini_key, "")); break;
                }
        }

        edit.apply ();
    }
}
