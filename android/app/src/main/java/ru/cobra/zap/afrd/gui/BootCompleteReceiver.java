package ru.cobra.zap.afrd.gui;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Build;

public class BootCompleteReceiver extends BroadcastReceiver
{
    @Override
    public void onReceive (Context context, Intent intent)
    {
        if (Intent.ACTION_BOOT_COMPLETED.equals (intent.getAction ()))
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
                context.startForegroundService (new Intent (context, AFRService.class));
            else
                context.startService (new Intent (context, AFRService.class));
    }
}
