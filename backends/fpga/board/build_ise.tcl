# Run with ISE14.7 xtclsh after sourcing its settings64.sh. No programming.
# MAX_LEVELS=1..10 selects an independent build/levelsN directory.
set root [file normalize [file join [file dirname [info script]] ..]]
set levels 10
if {[info exists ::env(MAX_LEVELS)]} { set levels $::env(MAX_LEVELS) }
if {[lsearch -exact {1 2 3 4 5 6 7 8 9 10} $levels] < 0} {
    puts stderr "ISE_BUILD_FAILED: MAX_LEVELS must be an integer in 1..10"
    exit 1
}
set build [file join $root build levels$levels]
set project_name thesis_tree_levels$levels

proc require_success {task} {
    set status [process get $task status]
    puts "ISE_STATUS: $task = $status"
    if {[lsearch -exact {up_to_date warnings} $status] < 0} {
        error "$task failed or is incomplete (status: $status)"
    }
}


# Fail closed on an absent/unparseable summary as well as known timing failure.
# Process status alone may be 'warnings' even when routed timing is violated.
proc verify_timing_report {path} {
    set handle [open $path r]
    set text [read $handle]
    close $handle
    set error_matches [regexp -all -inline -nocase {([0-9]+)[ \t]+timing errors? detected} $text]
    if {[llength $error_matches] == 0} {
        error "No timing-error summary in fresh report: $path"
    }
    foreach {match errors} $error_matches {
        if {$errors != 0} { error "Routed timing failed: $errors timing errors in $path" }
    }
    set period_matches [regexp -all -inline -nocase {Minimum period[ \t:]*(is[ \t]+)?([0-9]+([.][0-9]+)?)[ \t]*ns} $text]
    if {[llength $period_matches] == 0} {
        error "No minimum-period summary in fresh report: $path"
    }
    foreach {match is_word period fraction} $period_matches {
        if {$period > 20.0} {
            error "Routed minimum period ${period}ns exceeds 20ns target in $path"
        }
        puts "ISE_TIMING_GATE: errors=0 minimum_period_ns=$period target_ns=20 report=$path"
    }
}

if {[catch {
    file mkdir $build
    cd $build
    # Remove only disposable images and project metadata in this build scope.
    # A failed run must not expose a previously generated bitstream as fresh.
    foreach name [list mojo_top.bit mojo_top.bin $project_name.xise $project_name.gise] {
        set artifact [file join $build $name]
        if {[file exists $artifact]} { file delete -force $artifact }
    }
    # mtime has only whole-second precision in this Tcl version. Remove old
    # timing reports so even an equal timestamp cannot satisfy the fresh gate.
    foreach artifact [glob -nocomplain -directory $build *.twr] {
        file delete -force $artifact
    }
    set started [clock seconds]
    project new $project_name
    project set family Spartan6
    project set device xc6slx9
    project set package tqg144
    project set speed -2
    set sources [lsort [glob -nocomplain -directory [file join $root rtl] *.v]]
    foreach required {mojo_top.v tree_protocol.v thesis_tree_core.v} {
        if {![file isfile [file join $root rtl $required]]} {
            error "Missing RTL source: rtl/$required"
        }
    }
    lappend sources [file join $root board depth_tree_avr_bridge.v]
    # Upstream checkouts have used both src/ and src/UART/ layouts. Select
    # exactly one copy of each official helper, never vendor/mojo_top.v.
    foreach name {serial_rx.v serial_tx.v cclk_detector.v spi_slave.v} {
        set found {}
        foreach directory [list [file join $root vendor mojo-base-project src UART] \
                                [file join $root vendor mojo-base-project src]] {
            set candidate [file join $directory $name]
            if {[file isfile $candidate]} { set found $candidate; break }
        }
        if {$found eq {}} { error "Missing official UART helper: $name" }
        lappend sources $found
    }
    lappend sources [file join $root vendor mojo-base-project src mojo.ucf]
    foreach source $sources {
        if {![file isfile $source]} { error "Missing source: $source" }
        xfile add $source
    }
    project set top mojo_top
    project set "Generics, Parameters" "MAX_LEVELS=$levels" -process "Synthesize - XST"
    set generics [project get "Generics, Parameters" -process "Synthesize - XST"]
    if {[string first "MAX_LEVELS=$levels" $generics] < 0} {
        error "Top-level MAX_LEVELS override was not retained: $generics"
    }
    puts "ISE_CONFIGURATION: MAX_LEVELS=$levels; generic=$generics; build=$build"
    project set "Create Bit File" true
    project set "Create Binary Configuration File" true
    if {![process run "Generate Programming File" -force rerun_all]} {
        error "Generate Programming File failed to launch"
    }
    foreach task {"Synthesize - XST" "Translate" "Map" "Place & Route" "Generate Programming File"} {
        require_success $task
    }
    if {![process run "Generate Post-Place & Route Static Timing" -force rerun]} {
        error "Post-route static timing failed to launch"
    }
    require_success "Generate Post-Place & Route Static Timing"
    foreach name {mojo_top.bit mojo_top.bin} {
        set artifact [file join $build $name]
        if {![file isfile $artifact]} { error "Missing generated artifact: $artifact" }
        if {[file size $artifact] == 0 || [file mtime $artifact] < $started} {
            error "Empty or stale generated artifact: $artifact"
        }
        puts "ISE_ARTIFACT: $artifact ([file size $artifact] bytes)"
    }
    set fresh_timing 0
    foreach pattern {*.syr *.srp *.bld *_map.mrp *.par *.twr *.bgn} {
        foreach report [lsort [glob -nocomplain -directory $build $pattern]] {
            if {[file mtime $report] >= $started} {
                puts "ISE_REPORT: $report"
                if {[file extension $report] == ".twr"} {
                    verify_timing_report $report
                    set fresh_timing 1
                }
            }
        }
    }
    if {!$fresh_timing} { error "No fresh post-route timing report was generated" }
    puts "ISE_REVIEW_REQUIRED: inspect resource mapping, warnings, selected levels and 50 MHz timing before programming."
    project close
} failure]} {
    puts stderr "ISE_BUILD_FAILED: $failure"
    catch {project close}
    # A failed timing/process gate cannot leave an apparently usable image.
    foreach name {mojo_top.bit mojo_top.bin} {
        set artifact [file join $build $name]
        if {[file exists $artifact]} { file delete -force $artifact }
    }
    exit 1
}
