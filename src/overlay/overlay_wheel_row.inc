/* overlay_wheel_row.inc -- included inside draw_row (overlay_ui.cpp), after the selected row's highlight: the Wheel pages' two row
 * kinds. It uses draw_row's own locals (dl, r, sel, fs, lx, ty, cy, label_w, vr, bmid, bpad, bh, by0, by1, ah, LY, T) and returns
 * from draw_row for them. */
if (r.kind == R_BIND) {
    /* A binding: the label as an action's, the binding (or the prompt while it waits) in the value box. */
    char v[96];
    int locked = 0;

    wheel_bind_value(r.action, v, sizeof v, &locked);
    const bool waiting = wheel_capturing() && r.action == s_wb_cap;
    const ImU32 lc = locked ? col(T->text_dim) : sel ? col(T->text_hi) : col(T->text);
    {
        Face it(s_f_body, SHEAR);
        txt_fit(dl, fs, ImVec2(lx, ty), lc, caps(r.text), label_w);
    }
    value_box(dl, LY.x_val0, by0, vr, by1, sel);
    txt_fit(dl, fs, ImVec2(bmid, ty), waiting ? col(T->attn) : lc, v, vr - LY.x_val0 - 2.0f * bpad, false, true);
    if (locked)
        padlock_spr(dl, LY.x_val0 - ah * 0.9f, cy, bh * 0.9f);
    return;
}
if (r.kind == R_LIVE) {
    /* A live value: a bar in the value column (the middle marked when it runs both ways) with its number over it, or just text. */
    const WLive &w = s_wl[r.action];

    {
        Face it(s_f_body, SHEAR);
        txt_fit(dl, fs, ImVec2(lx, ty), sel ? col(T->text_hi) : col(T->text), caps(r.text), label_w);
    }
    if (w.bar) {
        const float x0 = LY.x_val0, x1 = vr, hh = bh * 0.55f;
        const ImVec2 a(x0, cy - hh * 0.5f), b(x1, cy + hh * 0.5f);

        dl->AddRectFilled(a, b, col(T->track), hh * 0.3f);
        if (w.bipolar) {
            const float mid = (x0 + x1) * 0.5f, px = mid + w.v * (x1 - x0) * 0.5f;

            dl->AddRectFilled(ImVec2(fminf(mid, px), a.y), ImVec2(fmaxf(mid, px), b.y), col(T->fill), hh * 0.3f);
            dl->AddLine(ImVec2(mid, a.y - hh * 0.2f), ImVec2(mid, b.y + hh * 0.2f), col(T->text_dim));
        } else if (w.v > 0.0f) {
            dl->AddRectFilled(a, ImVec2(x0 + (x1 - x0) * w.v, b.y), col(T->fill), hh * 0.3f);
        }
        txt_fit(dl, fs * 0.85f, ImVec2(bmid, top_for(cy, fs * 0.85f)), col(T->text_hi), w.val, x1 - x0 - 2.0f * bpad, false, true);
    } else {
        txt_fit(dl, fs, ImVec2(bmid, ty), sel ? col(T->text_hi) : col(T->text), w.val, vr - LY.x_val0, false, true);
    }
    return;
}
