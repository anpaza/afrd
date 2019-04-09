package ru.cobra.zap.xprefs;

import android.content.Context;
import android.preference.ListPreference;
import android.util.AttributeSet;
import android.view.View;
import android.widget.TextView;

import ru.cobra.zap.afrd.gui.R;

public class XListPreference extends ListPreference
{
    public XListPreference (Context context)
    {
        this (context, null);
    }

    public XListPreference (Context context, AttributeSet attrs)
    {
        super (context, attrs);
        setWidgetLayoutResource (R.layout.pref_value);
    }

    @Override
    protected void onBindView (View view)
    {
        super.onBindView (view);

        final TextView value = view.findViewById (R.id.value);
        value.setText (getEntry ());
    }
}
