package ru.cobra.zap.afrd;

import android.util.Log;

/**
 * Detector for steady failure conditions.
 *
 * If some heavy process is constantly failing, it's better to stop trying
 * than to continue doing that forever.
 *
 * This class implements the neccessary timings for this.
 */
public class FailureDetector
{
    /// The name of this failure condition
    private String mName;
    /// true if we should give up after many failures
    private boolean mGiveUp = false;
    /// The time when previous failure occured
    private long mFailureLastStamp = 0;
    /// Number of consecutive failures
    private int mFailureCount = 0;
    /// Max number of failures to shift into the 'give up' state
    private final int mMaxFailures = 3;
    /// the minimal interval between failures to take them into account
    private final int mFailureMinInterval = 1000;
    /// the maximal interval between failures to take them into account
    private final int mFailureMaxInterval = 10000;

    public FailureDetector (String name)
    {
        mName = name;
    }

    /**
     * Report a successful operation to the failure detector.
     * All failure counters are reset.
     */
    public void success ()
    {
        if (mGiveUp)
            Log.i ("afrd", mName + ": resetting failure condition");
        mFailureLastStamp = 0;
        mFailureCount = 0;
        mGiveUp = false;
    }

    /**
     * Report that the operation failed.
     * This counts failures and shifts into the 'give up' state if needed
     */
    public void failure ()
    {
        long now = System.currentTimeMillis ();
        if (mFailureLastStamp == 0)
        {
            mFailureLastStamp = now;
            mFailureCount = 1;
            return;
        }

        // don't account for failures happening too often
        if (now - mFailureLastStamp < mFailureMinInterval)
            return;

        // and don't count failures happening very seldom
        if (now - mFailureLastStamp > mFailureMaxInterval)
        {
            mFailureLastStamp = now;
            return;
        }

        mFailureLastStamp = now;
        mFailureCount++;
        if (mFailureCount >= mMaxFailures)
        {
            Log.e ("afrd", mName + ": Too many failures, giving up");
            mGiveUp = true;
        }
    }

    /**
     * Shift into the 'give up' state immediately.
     * This is called if a fatal error condition has been detected by the caller
     * and it is 100% sure there is no sense in trying anymore.
     */
    public void fatal ()
    {
        success ();
        mGiveUp = true;
    }

    /**
     * Check if there's no sense to try again
     */
    public boolean giveUp ()
    {
        return mGiveUp;
    }
}
