package ru.cobra.zap.afrd;

import android.content.res.Resources;
import android.util.Log;

import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.MappedByteBuffer;
import java.nio.channels.FileChannel;
import java.util.Locale;
import java.util.zip.CRC32;

import ru.cobra.zap.afrdctl.R;

/**
 * This class provides afrd status info.
 */
public class Status
{
    private static final String AFRD_SHM = "/dev/run/afrd.ipc";

    private RandomAccessFile mShmFile;
    private MappedByteBuffer mShm;
    private int mShmSize;
    private int mLastStamp;
    private boolean mLastStampValid = false;

    /// true if afrd is enabled
    public boolean mEnabled;
    /// true if current screen refresh rate is different from default
    public boolean mModified;
    /// true if screen is blackened while detecting movie frame rate
    public boolean mBlackened;
    /// afrd version high number
    public int mVersionHi;
    /// afrd version low number
    public int mVersionLo;
    /// afrd revision number
    public int mVersionRev;
    /// afrd version suffix
    public String mVersionSfx;
    /// Full version name
    public String mVersion;
    /// afrd build date
    public String mBuildDate;
    /// current screen refresh rate, .8 fixed-point
    public int mCurrentHz;
    /// original screen refresh rate, .8 fixed-point
    public int mOriginalHz;
    /// Failure detector to stop querying afrd status
    public static FailureDetector mFail = new FailureDetector ("daemon status");

    /**
     * Check if IPC channel has been successfully opened
     *
     * @return true if it was
     */
    public boolean ok ()
    {
        return (mShm != null);
    }

    /**
     * Open the IPC channel
     *
     * @return true on success
     */
    public boolean open ()
    {
        if (mFail.giveUp ())
            return false;

        close ();

        try
        {
            mShmFile = new RandomAccessFile (AFRD_SHM, "r");
            mShmSize = (int) mShmFile.length ();
            mShm = mShmFile.getChannel ().map (FileChannel.MapMode.READ_ONLY, 0, mShmSize);
            mShm.order (ByteOrder.LITTLE_ENDIAN);
            return true;
        }
        catch (IOException exc)
        {
            jfun.logExc ("ipc open", exc);

            Throwable cause = exc.getCause ();
            if ((cause != null) &&
                (cause.getMessage ().contains ("EACCES")))
            {
                Log.e ("afrd", "It seems like SELinux is enabled, giving up");
                mFail.fatal ();
            }
            else
            {
                mFail.failure ();
            }

            return false;
        }
    }

    /**
     * Close the IPC channel
     */
    public void close ()
    {
        mLastStampValid = false;
        mShm = null;
        if (mShmFile == null)
            return;

        try
        {
            mShmFile.close ();
        }
        catch (IOException exc)
        {
            jfun.logExc ("ipc close", exc);
        }
        mShmFile = null;
    }

    /**
     * Refresh afrd state using the IPC channel
     *
     * @return false if nothing has changed, true if new state has been read
     */
    public boolean refresh ()
    {
        if (mShm == null || mFail.giveUp ())
            return false;

        // check if buffer changed since our last update
        int stamp = mShm.getInt (0);
        if (mLastStampValid && (mLastStamp == stamp))
            return false;

        short size = mShm.getShort (4);
        if (size != mShmSize)
        {
            close ();
            return false;
        }

        // make a replica of the shared data
        byte[] data = new byte[size];
        for (int i = 0; i < size; i++)
            data[i] = mShm.get (i);
        ByteBuffer buff = ByteBuffer.wrap (data);
        buff.order (ByteOrder.LITTLE_ENDIAN);

        // now check if buffer is consistent
        stamp = buff.getInt (0);
        int stamp2 = buff.getInt (size - 4);
        if (stamp != stamp2)
            return false;

        CRC32 crc = new CRC32 ();
        crc.update (data, 4, size - 4 * 2);
        int crcval = (int) crc.getValue ();
        if (stamp != crcval)
            return false;

        mLastStamp = stamp;
        mLastStampValid = true;

        mEnabled = (buff.get (6) != 0);
        mModified = (buff.get (7) != 0);
        mBlackened = (buff.get (8) != 0);
        mVersionHi = buff.get (9);
        mVersionLo = buff.get (10);
        mVersionRev = buff.get (11);
        mBuildDate = jfun.cstr (data, 12, 24);
        mCurrentHz = buff.getInt (36);
        mOriginalHz = buff.getInt (40);
        if (mShmSize >= 44+8)
            mVersionSfx = jfun.cstr (data, 44, 8);
        else
            mVersionSfx = "";

        mVersion = String.format (Locale.getDefault (),
            "%d.%d.%d%s", mVersionHi, mVersionLo, mVersionRev, mVersionSfx);

        return true;
    }

    public static String hz2str (Resources res, int hz)
    {
        return res.getString (R.string.hz,
            hz >> 8, ((hz & 0xff) * 100 + 128) >> 8);
    }
}
