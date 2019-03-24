package ru.cobra.zap.afrdctl;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

public class BootCompleteReceiver extends BroadcastReceiver
{
    @Override
    public void onReceive (Context context, Intent intent)
    {
        if (Intent.ACTION_BOOT_COMPLETED.equals (intent.getAction ()))
            context.startService (new Intent (context, AFRService.class));
    }
}
