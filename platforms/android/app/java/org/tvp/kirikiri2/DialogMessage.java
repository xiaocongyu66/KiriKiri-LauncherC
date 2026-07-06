package org.tvp.kirikiri2;

import android.content.Context;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.LinearLayout;

import androidx.appcompat.app.AlertDialog;

import com.google.android.material.dialog.MaterialAlertDialogBuilder;

public class DialogMessage {
    public String Title;
    public String Text;
    public String[] Buttons;
    public EditText TextEditor = null;

    public void Init(final String title, final String text, final String[] buttons) {
        this.Title = title;
        this.Text = text;
        this.Buttons = buttons;
        this.TextEditor = null;
    }

    void onButtonClick(int n) {
        if (TextEditor != null) {
            NativeUiHost.onMessageBoxText(TextEditor.getText().toString());
        }
        NativeUiHost.onMessageBoxOK(n);
    }

    public MaterialAlertDialogBuilder CreateBuilder() {
        MaterialAlertDialogBuilder builder = new MaterialAlertDialogBuilder(NativeUiHost.requireActivityContext()).
                setTitle(Title).
                setMessage(Text).
                setCancelable(false);
        if (Buttons.length >= 1) {
            builder = builder.setPositiveButton(Buttons[0], (dialog, which) -> onButtonClick(0));
        }
        if (Buttons.length >= 2) {
            builder = builder.setNeutralButton(Buttons[1], (dialog, which) -> onButtonClick(1));
        }
        if (Buttons.length >= 3) {
            builder = builder.setNegativeButton(Buttons[2], (dialog, which) -> onButtonClick(2));
        }
        return builder;
    }

    public void ShowMessageBox() {
        CreateBuilder().create().show();
    }

    public void ShowInputBox(final String text) {
        MaterialAlertDialogBuilder builder = CreateBuilder();
        TextEditor = new EditText(NativeUiHost.requireActivityContext());
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.MATCH_PARENT);
        TextEditor.setLayoutParams(lp);
        TextEditor.setText(text);
        builder.setView(TextEditor);
        AlertDialog ad = builder.create();
        ad.show();
        TextEditor.requestFocus();
        InputMethodManager imm = (InputMethodManager) NativeUiHost.requireApplicationContext()
                .getSystemService(Context.INPUT_METHOD_SERVICE);
        imm.showSoftInput(TextEditor, 0);
    }
}
