__declspec(dllexport) void user_terminate(void) {
    if(background_process){CloseHandle(background_process);background_process=NULL;}
    local_control_stop();
    drawing_number_clear();
    bwtl_live_watch_stop();
    panel_flow_cancel();
    bwtl_save_watch_stop();
    fastener_pressfit_highlight_clear();
    markup_ai_cleanup();
    markup_capture_clear();
    if(markup_last_holes.stamp)ProVerstampFree(&markup_last_holes.stamp);
    foundation_reset();
    part_snapshot_clear(&request_snapshot);
    default_part_cancel();
    return_assembly_cancel();
    fastener_open_cancel();
    fastener_saved_clear();
    smart_cancel();native_attachments_clear();
    resident_shutdown();
    native_stop_poll();
    native_ui_stop_thread();
    if(native_backend_window)DestroyWindow(native_backend_window);
    UnregisterClassW(L"CreoCodexBackendV12",GetModuleHandleW(NULL));
    UnregisterClassW(L"CreoCodexNativeChatV11",GetModuleHandleW(NULL)); created=0;
    trace("user_terminate", 0);
    runtime_isolation_end();
}

