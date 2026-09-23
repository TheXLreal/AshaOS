/*
 * Copyright (C) 2026 The AshaOS Project
 * Licensed under the Apache License, Version 2.0.
 */
package org.ashaos.audioasha;

import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

/** Applies the selected theme and Android 16 system-bar/cutout safe area. */
final class UiAppearance {
    static final String PREF_UI_THEME = "ui_theme";

    private UiAppearance() {}

    static Context wrap(Context base) {
        SharedPreferences preferences = base.getSharedPreferences(
                AudioAshaService.PREFERENCES, Context.MODE_PRIVATE);
        Configuration configuration = new Configuration(base.getResources().getConfiguration());
        int theme = preferences.getInt(PREF_UI_THEME, 0);
        if (theme != 0) {
            configuration.uiMode = (configuration.uiMode & ~Configuration.UI_MODE_NIGHT_MASK)
                    | (theme == 2 ? Configuration.UI_MODE_NIGHT_YES
                            : Configuration.UI_MODE_NIGHT_NO);
        }
        return base.createConfigurationContext(configuration);
    }

    static void selectTheme(Activity activity) {
        activity.setTheme(isDark(activity) ? R.style.AppTheme_Dark : R.style.AppTheme);
    }

    static void applyInsets(Activity activity, View root) {
        boolean dark = isDark(activity);
        activity.getWindow().clearFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        activity.getWindow().setStatusBarColor(activity.getColor(R.color.ashaos_background));
        activity.getWindow().setNavigationBarColor(activity.getColor(R.color.ashaos_background));
        WindowInsetsController controller = activity.getWindow().getInsetsController();
        if (controller != null) {
            controller.setSystemBarsAppearance(
                    dark ? 0 : WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS,
                    WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS);
        }
        root.setOnApplyWindowInsetsListener((view, insets) -> {
            android.graphics.Insets safe = insets.getInsets(
                    WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            view.setPadding(safe.left, safe.top, safe.right, safe.bottom);
            return insets;
        });
        root.requestApplyInsets();
    }

    private static boolean isDark(Context context) {
        return (context.getResources().getConfiguration().uiMode
                & Configuration.UI_MODE_NIGHT_MASK) == Configuration.UI_MODE_NIGHT_YES;
    }
}
