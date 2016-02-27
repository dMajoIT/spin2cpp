package require Tk
package require ctext
package require autoscroll

set COMPILE "./bin/spin2cpp --noheader"
set OUTPUT "--asm"
set EXT ".pasm"
set radioOut 1

#
# read a file and return its text
# does UCS-16 to UTF-8 conversion
#
proc uread {name} {
    set encoding ""
    set f [open $name r]
    gets $f line
    if {[regexp \xFE\xFF $line] || [regexp \xFF\xFE $line]} {
	fconfigure $f -encoding unicode
	set encoding unicode
    }
    seek $f 0 start ;# rewind
    set text [read $f [file size $name]]
    close $f
    if {$encoding=="unicode"} {
	regsub -all "\uFEFF|\uFFFE" $text "" text
    }
    return $text
}

proc resetOutputVars { } {
    global OUTPUT
    global EXT
    global radioOut
    global SPINFILE
    global PASMFILE
    
    if { $radioOut == 1 } {
	set OUTPUT "--asm"
	set EXT ".pasm"
    }
    if { $radioOut == 2 } {
	set OUTPUT "--ccode"
	set EXT ".c"
    }
    if { $radioOut == 3 } {
	set OUTPUT "--normalize"
	set EXT ".cpp"
    }
    set PASMFILE ""
    if { [string length $SPINFILE] != 0 } {
	regenOutput $SPINFILE
    }
}

proc loadFileToWindow { fname win } {
    set file_data [uread $fname]
    $win replace 1.0 end $file_data
}

proc saveFileFromWindow { fname win } {
    set fp [open $fname w]
    set file_data [$win get 1.0 end]
    puts -nonewline $fp $file_data
    close $fp
    regenOutput $fname
}

proc regenOutput { spinfile } {
    global COMPILE
    global PASMFILE
    global OUTPUT
    global EXT
    
    set outname $PASMFILE
    if { [string length $outname] == 0 } {
	set dirname [file dirname $spinfile]
	set outname [file rootname $spinfile]
	set outname "$outname$EXT"
	set PASMFILE $outname
    }
    set errout ""
    set status 0
    set cmdline "$COMPILE $OUTPUT -o $PASMFILE $spinfile"
    .bot.txt replace 1.0 end "$cmdline\n"
    if {[catch {exec -ignorestderr {*}$cmdline 2>@1} errout options]} {
	set status 1
    }
    .bot.txt insert 2.0 $errout
    if { $status != 0 } {
	tk_messageBox -icon error -type ok -message "Compilation failed"
    } else {
	loadFileToWindow $outname .out.txt
    }
}

set SpinTypes {
    {{Spin files}   {.spin} }
    {{All files}    *}
}

proc loadNewSpinFile {} {
    global SPINFILE
    global SpinTypes
    set filename [tk_getOpenFile -filetypes $SpinTypes -defaultextension ".spin" ]
    if { [string length $filename] == 0 } {
	return
    }
    loadFileToWindow $filename .orig.txt
    .orig.txt highlight 1.0 end
    regenOutput $filename
    set SPINFILE $filename
    wm title . $SPINFILE
}

proc saveSpinFile {} {
    global SPINFILE
    global SpinTypes
    
    if { [string length $SPINFILE] == 0 } {
	set filename [tk_getSaveFile -initialfile $SPINFILE -filetypes $SpinTypes -defaultextension ".spin" ]
	if { [string length $filename] == 0 } {
	    return
	}
	set SPINFILE $filename
    }
    
    saveFileFromWindow $SPINFILE .orig.txt
    wm title . $SPINFILE
}

proc saveSpinAs {} {
    global SPINFILE
    global SpinTypes
    set filename [tk_getSaveFile -filetypes $SpinTypes -defaultextension ".spin" ]
    if { [string length $filename] == 0 } {
	return
    }
    set SPINFILE $filename
    wm title . $SPINFILE
    saveSpinFile
}

set aboutMsg {
Convert .spin to PASM/C/C++
Copyright 2011-2016 Total Spectrum Software Inc.
------
This is an incomplete preview version!
There is no warranty and no guarantee that
output will be correct.    
}

proc doAbout {} {
    global aboutMsg
    tk_messageBox -icon info -type ok -message "Spin Converter" -detail $aboutMsg
}

proc doHelp {} {
    if {[winfo exists .help]} {
	raise .help
	return
    }
    toplevel .help
    frame .help.f
    text .help.f.txt -wrap none -yscroll { .help.f.v set } -xscroll { .help.f.h set }
    scrollbar .help.f.v -orient vertical -command { .help.f.txt yview }
    scrollbar .help.f.h -orient horizontal -command { .help.f.txt xview }

    grid columnconfigure .help {0 1} -weight 1
    grid rowconfigure .help 0 -weight 1
    grid .help.f -sticky nsew
    
    grid .help.f.txt .help.f.v -sticky nsew
    grid .help.f.h -sticky nsew
    grid rowconfigure .help.f .help.f.txt -weight 1
    grid columnconfigure .help.f .help.f.txt -weight 1

    loadFileToWindow README.txt .help.f.txt
    wm title .help "Spin Converter help"
}

#
# set up syntax highlighting for a given ctext widget
proc setHighlightingSpin {w} {
    set color(keywords) blue
    set color(brackets) purple
    set color(operators) green
    set color(comments) DeepPink
    set color(strings)  red
    set keywordsbase [list con obj dat pub pri quit exit repeat while until if then else return abort long word byte]
    foreach i $keywordsbase {
	lappend keywordsupper [string toupper $i]
    }
    set keywords [concat $keywordsbase $keywordsupper]
    
    ctext::addHighlightClass $w keywords $color(keywords) $keywords
    ctext::addHighlightClassForSpecialChars $w brackets $color(brackets) {[](){}}
    ctext::addHighlightClassForSpecialChars $w operators $color(operators) {+-=><!@~\#*/&:|}
    ctext::addHighlightClassForRegexp $w comments $color(comments) {\'[^\n\r]*}
    ctext::addHighlightClassForRegexp $w strings $color(strings) {"(\\"||^"])*"}
}

menu .mbar
. configure -menu .mbar
menu .mbar.file -tearoff 0
menu .mbar.edit -tearoff 0
menu .mbar.options -tearoff 0
menu .mbar.help -tearoff 0

.mbar add cascade -menu .mbar.file -label File
.mbar.file add command -label "Open Spin..." -accelerator "^O" -command { loadNewSpinFile }
.mbar.file add command -label "Save Spin" -accelerator "^S" -command { saveSpinFile }
.mbar.file add command -label "Save Spin As..." -command { saveSpinAs }
.mbar.file add separator
.mbar.file add command -label Exit -accelerator "^Q" -command { exit }

.mbar add cascade -menu .mbar.edit -label Edit
.mbar.edit add command -label "Cut" -accelerator "^X" -command {event generate [focus] <<Cut>>}
.mbar.edit add command -label "Copy" -accelerator "^C" -command {event generate [focus] <<Copy>>}
.mbar.edit add command -label "Paste" -accelerator "^V" -command {event generate [focus] <<Paste>>}

.mbar add cascade -menu .mbar.options -label Options
.mbar.options add radiobutton -label "Pasm Output" -variable radioOut -value 1 -command { resetOutputVars }
.mbar.options add radiobutton -label "C Output" -variable radioOut -value 2 -command { resetOutputVars }
.mbar.options add radiobutton -label "C++ Output" -variable radioOut -value 3 -command { resetOutputVars }

.mbar add cascade -menu .mbar.help -label Help
.mbar.help add command -label "Help" -command { doHelp }
.mbar.help add separator
.mbar.help add command -label "About..." -command { doAbout }

wm title . "Spin Converter"

grid columnconfigure . {0 1} -weight 1
grid rowconfigure . 0 -weight 1
frame .orig
frame .out
frame .bot

grid .orig -column 0 -row 0 -columnspan 1 -rowspan 1 -sticky nsew
grid .out -column 1 -row 0 -columnspan 1 -rowspan 1 -sticky nsew
grid .bot -column 0 -row 1 -columnspan 2 -sticky nsew

scrollbar .orig.v -orient vertical -command {.orig.txt yview}
scrollbar .orig.h -orient horizontal -command {.orig.txt xview}
ctext .orig.txt -wrap none -xscroll {.orig.h set} -yscrollcommand {.orig.v set}
label .orig.label -background DarkGrey -foreground white -text "Original Spin"
grid .orig.label       -sticky nsew
grid .orig.txt .orig.v -sticky nsew
grid .orig.h           -sticky nsew
grid rowconfigure .orig .orig.txt -weight 1
grid columnconfigure .orig .orig.txt -weight 1


scrollbar .out.v -orient vertical -command {.out.txt yview}
scrollbar .out.h -orient horizontal -command {.out.txt xview}
text .out.txt -wrap none -xscroll {.out.h set} -yscroll {.out.v set}
label .out.label -background DarkGrey -foreground white -text "Converted Code"
grid .out.label       -sticky nsew
grid .out.txt .out.v  -sticky nsew
grid .out.h           -sticky nsew
grid rowconfigure .out .out.txt -weight 1
grid columnconfigure .out .out.txt -weight 1

scrollbar .bot.v -orient vertical -command {.bot.txt yview}
scrollbar .bot.h -orient horizontal -command {.bot.txt xview}
text .bot.txt -wrap none -xscroll {.bot.h set} -yscroll {.bot.v set} -height 4

grid .bot.txt .bot.v -sticky nsew
grid .bot.h -sticky nsew
grid rowconfigure .bot .bot.txt -weight 1
grid columnconfigure .bot .bot.txt -weight 1


bind . <Control-o> { loadNewSpinFile }
bind . <Control-s> { saveSpinFile }
bind . <Control-q> { exit }

autoscroll::autoscroll .orig.v
autoscroll::autoscroll .orig.h
autoscroll::autoscroll .out.v
autoscroll::autoscroll .out.h
autoscroll::autoscroll .bot.v
autoscroll::autoscroll .bot.h

#setHighlightingSpin .orig.txt

set PASMFILE ""

if { $::argc > 0 } {
    loadFileToWindow $argv .orig.txt
    regenOutput $argv[1]
} else {
    set SPINFILE ""
}
