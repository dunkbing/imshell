namespace imshell::detail {
    // shortcut: windows (SPI_GETCLIENTAREAANIMATION), gtk (gtk-enable-animations) and android (animator_duration_scale) are not read yet, so motion stays on; IMSHELL_REDUCE_MOTION=1 turns it off
    bool SystemReduceMotion() {
        return false;
    }
} // namespace imshell::detail
