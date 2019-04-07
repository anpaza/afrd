package ru.cobra.zap.afrd.gui;

import android.app.Fragment;
import android.content.Context;
import android.os.Bundle;
import android.os.Handler;
import android.text.method.ScrollingMovementMethod;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;

import eu.chainfire.libsuperuser.Shell;
import ru.cobra.zap.afrd.Control;

public class LogFragment extends Fragment
{
    private static final String ARG_LOGFN = "log.fn";
    private static final String ARG_LOG_ENABLED = "log.enabled";

    private boolean mAttached = false;
    private Handler mTimer = new Handler ();
    private boolean mEnabled;
    private File mLog;
    private FileInputStream mLogStream;
    private int mLogSize;
    private TextView mTextLog;
    private TextView mTitle;

    /**
     * Create a log viewer fragment.
     *
     * @param logfn Parameter 1.
     * @param enabled whenever logging is enabled in daemon
     * @return A new instance of LogFragment.
     */
    public static LogFragment create (String logfn, boolean enabled)
    {
        if (logfn == null)
            return null;

        LogFragment fragment = new LogFragment ();
        Bundle args = new Bundle ();
        args.putString (ARG_LOGFN, logfn);
        args.putBoolean (ARG_LOG_ENABLED, enabled);
        fragment.setArguments (args);
        return fragment;
    }

    @Override
    public void onCreate (Bundle savedInstanceState)
    {
        super.onCreate (savedInstanceState);
        if (savedInstanceState == null)
            savedInstanceState = getArguments ();
        if (savedInstanceState != null)
        {
            String fn = savedInstanceState.getString (ARG_LOGFN);
            if (fn != null)
                mLog = new File (fn);
            mEnabled = savedInstanceState.getBoolean (ARG_LOG_ENABLED);
        }
    }

    @Override
    public void onAttach (Context context)
    {
        super.onAttach (context);
        mAttached = true;
    }

    @Override
    public void onDetach ()
    {
        super.onDetach ();
        mAttached = false;
        close ();
    }

    @Override
    public View onCreateView (LayoutInflater inflater, ViewGroup container,
        Bundle savedInstanceState)
    {
        // Inflate the layout for this fragment
        View root = inflater.inflate (R.layout.fragment_log, container, false);
        mTextLog = root.findViewById (R.id.textLog);
        mTextLog.setMovementMethod (new ScrollingMovementMethod ());
        mTitle = root.findViewById (R.id.logTitle);

        mTimer.post (new Runnable ()
        {
            @Override
            public void run ()
            {
                refresh ();
                mTimer.postDelayed (this, 500);
            }
        });

        View logClear = root.findViewById (R.id.buttonLogClear);
        if (logClear != null)
            logClear.setOnClickListener (new View.OnClickListener ()
            {
                @Override
                public void onClick (View v)
                {
                    logClear ();
                }
            });

        return root;
    }

    @Override
    public void onDestroyView ()
    {
        super.onDestroyView ();
        mTextLog = null;
        close ();
    }

    private boolean open ()
    {
        close ();

        if (!mAttached || (mTextLog == null))
            return false;

        mTitle.setText (
            String.format (getString (R.string.log_title),
                mLog.getPath (), mEnabled ? "" : getString (R.string.log_is_off)));

        try
        {
            mLogStream = new FileInputStream (mLog);
            mTextLog.setText ("");
            mLogSize = 0;
            return true;
        }
        catch (IOException ignored)
        {
        }

        mLogStream = null;
        mTextLog.setText (R.string.error_opening_log);
        return false;
    }

    private void close ()
    {
        try
        {
            if (mLogStream != null)
            {
                mLogStream.close ();
                mLogStream = null;
            }
        }
        catch (IOException ignored)
        {
        }

        if (mTextLog != null)
            mTextLog.setText ("");
    }


    private void refresh ()
    {
        if (!mAttached || mTextLog == null)
            return;

        if (mLogStream == null)
            if (!open ())
                return;

        try
        {
            int size = (int) mLog.length ();
            if (size < mLogSize)
                if (!open ())
                    return;

            int avail = size - mLogSize;
            if (avail <= 0)
                return;

            mLogSize = size;

            byte[] data = new byte[avail];
            mLogStream.read (data);
            String str = new String (data, StandardCharsets.UTF_8);
            mTextLog.append (str);
        }
        catch (IOException ignored)
        {
            close ();
        }
    }

    private void logClear ()
    {
        close ();

        String[] cmd = new String [] { "rm -f '" + mLog.getPath ().replace ("'", "'\"'\"'") + "'" };
        Shell.run ("su", cmd, null, false);

        Control ctl = new Control (getActivity ());
        ctl.reload ();

        refresh ();
    }
}
