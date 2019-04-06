package ru.cobra.zap.afrdctl;

import android.app.Fragment;
import android.content.Context;
import android.os.Bundle;
import android.os.Handler;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;
import android.widget.ToggleButton;

import java.util.Locale;

import ru.cobra.zap.afrd.Status;

public class StatusFragment extends Fragment
{
    private boolean mAttached = false;
    private TextView mVersion, mBuildDate, mCurrentHz, mOriginalHz;
    private ToggleButton mEnabled, mModified, mBlackened;
    private View mNoSignal;
    private Handler mTimer = new Handler ();
    private Status mAfrdStatus = new Status ();

    static StatusFragment create ()
    {
        StatusFragment fragment = new StatusFragment ();
        Bundle args = new Bundle ();
        fragment.setArguments (args);
        return fragment;
    }

    @Override
    public void onCreate (Bundle savedInstanceState)
    {
        super.onCreate (savedInstanceState);
    }

    @Override
    public View onCreateView (LayoutInflater inflater, ViewGroup container,
        Bundle savedInstanceState)
    {
        // Inflate the layout for this fragment
        View root = inflater.inflate (R.layout.fragment_status, container, false);
        mVersion = root.findViewById (R.id.textVersion);
        mBuildDate = root.findViewById (R.id.textBuildDate);
        mCurrentHz = root.findViewById (R.id.textCurrentHz);
        mOriginalHz = root.findViewById (R.id.textOriginalHz);
        mEnabled = root.findViewById (R.id.toggleEnabled);
        mModified = root.findViewById (R.id.toggleModified);
        mBlackened = root.findViewById (R.id.toggleBlackened);
        mNoSignal = root.findViewById (R.id.no_signal);
        return root;
    }

    @Override
    public void onAttach (Context context)
    {
        super.onAttach (context);
        mAttached = true;
        mTimer.post (new Runnable ()
        {
            @Override
            public void run ()
            {
                update ();
                mTimer.postDelayed (this, 500);
            }
        });
    }

    @Override
    public void onDetach ()
    {
        super.onDetach ();
        mAttached = false;
        mAfrdStatus.close ();
    }

    private void update ()
    {
        if (!mAttached)
            return;

        if (!mAfrdStatus.ok () && !mAfrdStatus.open ())
        {
            mNoSignal.setVisibility (View.VISIBLE);
            return;
        }

        if (!mAfrdStatus.refresh ())
            return;

        mNoSignal.setVisibility (View.GONE);

        mEnabled.setChecked (mAfrdStatus.mEnabled);
        mModified.setChecked (mAfrdStatus.mModified);
        mBlackened.setChecked (mAfrdStatus.mBlackened);
        mVersion.setText (mAfrdStatus.mVersion);
        mBuildDate.setText (mAfrdStatus.mBuildDate);
        mCurrentHz.setText (Status.hz2str (getResources (), mAfrdStatus.mCurrentHz));
        mOriginalHz.setText (Status.hz2str (getResources (), mAfrdStatus.mOriginalHz));
    }
}
