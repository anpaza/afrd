package ru.cobra.zap.afrd.gui;

import android.app.Fragment;
import android.os.Bundle;
import android.os.Handler;
import android.util.ArrayMap;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.SimpleAdapter;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Vector;

public class FAQFragment extends Fragment
{
    private static class Question
    {
        String headline;
        String question;
        Vector<String> answer;

        Question (String _headline)
        {
            headline = _headline;
            answer = new Vector<> ();
        }

        void addQuestion (String line)
        {
            if (question == null)
                question = line;
            else
                question = question + " " + line;
        }

        void addAnswer (String line)
        {
            answer.add (line);
        }

        void appendAnswer (String line)
        {
            String ans = answer.elementAt (answer.size () - 1);
            if (!ans.isEmpty ())
                ans += " ";
            ans += line;
            answer.set (answer.size () - 1, ans);
        }
    }

    private ListView mQuestList;
    private LinearLayout mAnswer;
    private Vector<Question> mFAQ;
    private Handler mTimer = new Handler ();
    private int mQuestionIndex = -1;
    private int mAnswerIndex;

    public static FAQFragment create ()
    {
        return new FAQFragment ();
    }

    @Override
    public View onCreateView (LayoutInflater inflater, ViewGroup container,
        Bundle savedInstanceState)
    {
        View root = inflater.inflate (R.layout.fragment_faq, container, false);

        mQuestList = root.findViewById (R.id.qlist);
        mAnswer = root.findViewById (R.id.answer);

        loadFaq ();
        fillQuestions ();

        return root;
    }

    private void fillQuestions ()
    {
        List<Map<String, String>> data = new ArrayList<> ();
        for (Question q : mFAQ)
        {
            Map<String, String> menuent;
            menuent = new ArrayMap<> (2);
            menuent.put ("item", q.headline);
            menuent.put ("desc", q.question);
            data.add (menuent);
        }

        SimpleAdapter adapter = new SimpleAdapter (getContext (), data, android.R.layout.simple_list_item_2,
            new String[] { "item", "desc" }, new int[] { android.R.id.text1, android.R.id.text2 });
        mQuestList.setAdapter(adapter);
        mQuestList.setOnItemClickListener (new AdapterView.OnItemClickListener ()
        {
            @Override
            public void onItemClick (AdapterView<?> parent, View view, int position, long id)
            {
                showAnswer (position);
            }
        });
    }

    final class AnswerRunnable implements Runnable
    {
        @Override
        public void run ()
        {
            showAnswer (mQuestionIndex);
        }
    };
    AnswerRunnable mAnsRun = new AnswerRunnable ();

    private void showAnswer (int position)
    {
        int delay;
        if (position != mQuestionIndex)
        {
            mAnswer.removeAllViews ();
            mQuestionIndex = position;
            mAnswerIndex = 0;
            mTimer.removeCallbacks (mAnsRun);
            delay = 250;
        }
        else
        {
            Question q = mFAQ.elementAt (position);
            if (q.answer.size () <= mAnswerIndex)
            {
                // finita la comedia
                mQuestionIndex = -1;
                return;
            }

            int dp16 = (int) TypedValue.applyDimension (
                TypedValue.COMPLEX_UNIT_DIP, 16, getResources ().getDisplayMetrics ());
            TextView tv = new TextView (getContext ());
            tv.setText (q.answer.elementAt (mAnswerIndex++));
            tv.setTextSize (TypedValue.COMPLEX_UNIT_SP, 18);
            tv.setGravity (Gravity.CENTER_VERTICAL | Gravity.START);
            tv.setPadding (dp16, dp16 / 4, dp16, dp16 / 4);
            mAnswer.addView (tv);
            delay = 1000;
        }

        mTimer.postDelayed (mAnsRun, delay);
    }

    private enum ReadMode
    {
        Init, Question, Answer, AnswerNewLine
    };

    private void loadFaq ()
    {
        mFAQ = new Vector<> ();

        try
        {
            InputStream is = getResources ().openRawResource (R.raw.faq);
            BufferedReader bufr = new BufferedReader (new InputStreamReader (is, StandardCharsets.UTF_8));
            String line;
            Question q = null;
            ReadMode mode = ReadMode.Init;
            while ((line = bufr.readLine ()) != null)
            {
                line = line.trim ();
                if (line.length () == 0)
                {
                    if (mode == ReadMode.Answer)
                        mode = ReadMode.AnswerNewLine;
                }
                else
                {
                    char type = line.charAt (0);
                    line = line.substring (1).trim ();
                    switch (type)
                    {
                        case '?':
                            if (mode != ReadMode.Question)
                            {
                                if (q != null)
                                    mFAQ.add (mFAQ.size (), q);

                                q = new Question (line);
                                mode = ReadMode.Question;
                            }
                            else
                                q.addQuestion (line);
                            break;

                        case '!':
                            switch (mode)
                            {
                                case Init:
                                    // ignore, should never happen
                                    break;

                                case Question:
                                case AnswerNewLine:
                                    q.addAnswer (line);
                                    mode = ReadMode.Answer;
                                    break;

                                case Answer:
                                    q.appendAnswer (line);
                                    break;
                            }
                            break;
                    }
                }
            }

            if (q != null)
                mFAQ.add (mFAQ.size (), q);

            is.close ();
        }
        catch (IOException ignored)
        {
        }
    }
}
