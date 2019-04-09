package ru.cobra.zap.xprefs;

import android.content.Context;
import android.preference.EditTextPreference;
import android.util.AttributeSet;
import android.view.View;
import android.widget.TextView;

import ru.cobra.zap.afrd.gui.R;

public class XEditTextPreference extends EditTextPreference
{
    public XEditTextPreference (Context context)
    {
        this (context, null);
    }

    public XEditTextPreference (Context context, AttributeSet attrs)
    {
        super (context, attrs);
        setWidgetLayoutResource (R.layout.pref_value);
    }

    @Override
    protected void onBindView (View view)
    {
        super.onBindView (view);

        final TextView value = view.findViewById (R.id.value);
        value.setText (getText ());
    }
}
