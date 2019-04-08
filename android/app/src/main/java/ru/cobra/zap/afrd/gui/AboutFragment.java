/*
 * Automatic Framerate Daemon for AMLogic S905/S912-based boxes.
 * Copyright (C) 2017-2019 Andrey Zabolotnyi <zapparello@ya.ru>
 *
 * For copying conditions, see file COPYING.txt.
 */

package ru.cobra.zap.afrd.gui;

import android.app.Fragment;
import android.os.Bundle;
import android.os.Handler;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;

public class AboutFragment extends Fragment
{
    private final ArrayList<View> mAbouts = new ArrayList<> ();
    private int mAboutsIndex;
    private boolean mAboutsShow;
    private Handler mTimer = new Handler ();
    private TextView mLicenseText;
    private LinearLayout mAboutBottom;
    private Button mShowAll;
    private boolean mLicenseDisplayed = false;

    public static AboutFragment create ()
    {
        return new AboutFragment ();
    }

    @Override
    public View onCreateView (LayoutInflater inflater, ViewGroup container,
        Bundle savedInstanceState)
    {
        View root = inflater.inflate (R.layout.fragment_about, container, false);

        // prepare the authors animation
        final LinearLayout thanks = root.findViewById (R.id.about_thanks);
        mAbouts.clear ();
        mAboutsIndex = 1000;
        mAboutsShow = true;
        for (int i = 0; i < thanks.getChildCount (); i++)
        {
            final View c = thanks.getChildAt (i);
            c.setVisibility (View.GONE);
            if (c instanceof LinearLayout)
                mAbouts.add (c);
        }

        animate ();

        TextView whatsnew = root.findViewById (R.id.whatsnew);
        whatsnew.setText (readFileJoinLines (R.raw.changelog));

        mLicenseText = root.findViewById (R.id.license);
        mAboutBottom = root.findViewById (R.id.about_bottom);

        mShowAll = root.findViewById (R.id.butLicense);
        mShowAll.setOnClickListener (new View.OnClickListener ()
        {
            @Override
            public void onClick (View v)
            {
                toggleLicense ();
            }
        });

        mLicenseText.setOnClickListener (new View.OnClickListener ()
        {
            @Override
            public void onClick (View v)
            {
                toggleLicense ();
            }
        });

        return root;
    }

    private void toggleLicense ()
    {
        mLicenseDisplayed = !mLicenseDisplayed;
        if (mLicenseDisplayed)
        {
            mLicenseText.setText (readFile (R.raw.copying));
            mShowAll.setText (R.string.about_hidelic);
            mAboutBottom.setVisibility (View.GONE);
        }
        else
        {
            mLicenseText.setText (getString (R.string.about_gpl3));
            mShowAll.setText (R.string.about_showlic);
            mAboutBottom.setVisibility (View.VISIBLE);
        }
    }

    private String readFile (int resid)
    {
        try
        {
            InputStream is = getResources ().openRawResource (resid);
            byte[] buff = new byte[is.available ()];
            if (is.read (buff) < 0)
                return "";

            return new String (buff, StandardCharsets.UTF_8);
        }
        catch (IOException ignored)
        {
        }

        return "";
    }

    private String readFileJoinLines (int resid)
    {
        String ret = readFile (resid);
        // Remove newlines not followed by another newline
        return ret.replaceAll (" *([^\n])\n +", "$1 ");
    }

    private void animate ()
    {
        int delay;

        if (mAboutsShow)
        {
            mAboutsIndex += 1;
            if (mAboutsIndex >= mAbouts.size ())
            {
                mAboutsIndex = -1;
                delay = 10000;
            }
            else
            {
                View v = mAbouts.get (mAboutsIndex);
                v.setVisibility (View.VISIBLE);
                mAboutsShow = false;
                delay = 5000;
            }
        }
        else
        {
            View v = mAbouts.get (mAboutsIndex);
            v.setVisibility (View.GONE);
            mAboutsShow = true;
            delay = 1000;
        }

        mTimer.postDelayed (new Runnable ()
        {
            @Override
            public void run ()
            {
                animate ();
            }
        }, delay);
    }
}
