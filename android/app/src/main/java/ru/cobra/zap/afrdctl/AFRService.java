package ru.cobra.zap.afrdctl;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.drawable.Icon;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.widget.Toast;

import ru.cobra.zap.afrd.Control;
import ru.cobra.zap.afrd.Status;

public class AFRService extends Service
{
    public static final int NOTIF_STATUS_ID = 1;
    public static final String NOTIF_CHANNEL = "AFRd";
    private Handler mTimer = new Handler ();
    private Control mControl;
    private Status mStatus = new Status ();
    private int oldRefreshRate = -1;
    private SharedPreferences mOptions;

    @Override
    public int onStartCommand (Intent intent, int flags, int startId)
    {
        return super.onStartCommand (intent, flags, startId);
    }

    @Override
    public void onCreate ()
    {
        super.onCreate ();

        mControl = new Control (this);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
        {
            NotificationManager notificationManager =
                (NotificationManager) getSystemService (Context.NOTIFICATION_SERVICE);
            if (notificationManager != null)
            {
                NotificationChannel notificationChannel = new NotificationChannel (
                    NOTIF_CHANNEL, getString (R.string.notif_channel), NotificationManager.IMPORTANCE_MIN);
                notificationManager.createNotificationChannel (notificationChannel);
            }
        }

        mOptions = getSharedPreferences ("ini_options", 0);

        mTimer.post (new Runnable ()
        {
            @Override
            public void run ()
            {
                checkDaemon ();
                updateNotification ();
                mTimer.postDelayed (this, 1000);
            }
        });
    }

    private void updateNotification ()
    {
        if (!mStatus.ok ())
            mStatus.open ();

        if (!mStatus.refresh ())
            return;

        if (oldRefreshRate != mStatus.mCurrentHz)
        {
            boolean show_toast = (oldRefreshRate != -1) &&
                mOptions.getBoolean ("toast_hz", true);

            oldRefreshRate = mStatus.mCurrentHz;

            if (show_toast)
                mTimer.post (new Runnable ()
                {
                    final int current_hz = mStatus.mCurrentHz;

                    @Override
                    public void run ()
                    {
                        Toast.makeText (getApplicationContext (),
                            Status.hz2str (getResources (), current_hz),
                            Toast.LENGTH_LONG).show ();
                    }
                });
        }

        // When user presses the notification, the main activity shows up
        Intent runApp = new Intent (this, MainActivity.class);
        runApp.setFlags (Intent.FLAG_ACTIVITY_SINGLE_TOP);

        Intent startStop = new Intent (mStatus.mEnabled ?
            ActionActivity.INTENT_STOP : ActionActivity.INTENT_START);
        startStop.setFlags (Intent.FLAG_ACTIVITY_NO_HISTORY |
            Intent.FLAG_ACTIVITY_EXCLUDE_FROM_RECENTS |
            Intent.FLAG_ACTIVITY_NO_ANIMATION);

        Notification.Action action = new Notification.Action.Builder (
            Icon.createWithResource (this, mStatus.mEnabled ? R.drawable.ic_stop : R.drawable.ic_play),
            getString (mStatus.mEnabled ? R.string.notif_stop_afrd : R.string.notif_start_afrd),
            PendingIntent.getActivity (this, 0, startStop, 0)).build ();

        Notification.Builder b;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
            b = new Notification.Builder (this, NOTIF_CHANNEL);
        else
            b = new Notification.Builder (this);

        Notification n = b
                .setSmallIcon (R.drawable.ic_afrd)
                .setLargeIcon (Icon.createWithResource (this, R.drawable.ic_afrd))
                .setContentIntent (PendingIntent.getActivity (this, 0, runApp, 0))
                .setOngoing (true)
                .setOnlyAlertOnce (true)
                .setLocalOnly (true)
                .setCategory (Notification.CATEGORY_SERVICE)
                .setContentTitle (getString (mStatus.mEnabled ? R.string.notif_enabled : R.string.notif_disabled))
                .setContentText (getResources ().getString (R.string.notif_hz,
                    Status.hz2str (getResources (), mStatus.mCurrentHz),
                    Status.hz2str (getResources (), mStatus.mOriginalHz)))
                .addAction (action)
                .build ();

        startForeground (NOTIF_STATUS_ID, n);
    }

    @Override
    public IBinder onBind (Intent intent)
    {
        // TODO: Return the communication channel to the service.
        throw new UnsupportedOperationException ("Not yet implemented");
    }

    private void checkDaemon ()
    {
        if (mControl.isRunning ())
            return;

        mControl.restart ();
    }
}