/* Copyright (C) 2026 The AshaOS Project. Licensed under Apache-2.0. */
package org.ashaos.audioasha;

import android.view.View;
import android.widget.AdapterView;

/** Small Java callback adapter used by the XML-view UI. */
final class SimpleItemSelectedListener implements AdapterView.OnItemSelectedListener {
    interface Callback {
        void onSelected(int position);
    }

    private final Callback mCallback;

    SimpleItemSelectedListener(Callback callback) {
        mCallback = callback;
    }

    @Override
    public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
        mCallback.onSelected(position);
    }

    @Override
    public void onNothingSelected(AdapterView<?> parent) {
        mCallback.onSelected(0);
    }
}
