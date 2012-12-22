# ~/.bashrc: executed by bash(1) for non-login shells.
# Commands that affect only login shells should go in .bash_profile.
# This script should NOT output to the screen.

# If not running interactively, don't do anything
[ -z "$PS1" ] && return

[[ $HOSTNAME == "burrard" ]] && is_syslab=yes

PATH=$HOME/bin:$HOME/usr/bin:$HOME/scripts:$HOME/installations/javacc-5.0/bin:$HOME/android-sdk-linux_x86/tools:$PATH
if [[ -n "$is_syslab" ]]; then
	export PATH=$PATH:/scratch/pjh/bin:/scratch/pjh/usr/bin:/scratch/pjh/parsec-2.1/bin:/scratch/pjh/NX/bin:/scratch/pjh/bin/depot_tools
fi
export PATH

CLASSPATH=$HOME/java/crawler4j-2.6.1/*:$HOME/java/lib/*; export CLASSPATH
    # Note to self: got classpath problems? Look at the javac man page!
PYTHONPATH=$HOME/lib/python/site-packages/; export PYTHONPATH
PKG_CONFIG_PATH=$HOME/lib/pkgconfig; export PKG_CONFIG_PATH
LD_RUN_PATH=$HOME/lib:$HOME/research/nvm/novaOS/keyvalue/src/leveldb/lib; export LD_RUN_PATH
LD_LIBRARY_PATH=$HOME/lib:$HOME/research/nvm/novaOS/keyvalue/src/leveldb/lib; export LD_LIBRARY_PATH

export TERM=xterm-256color

# don't put duplicate lines in the history. See bash(1) for more options
# ... or force ignoredups and ignorespace
HISTCONTROL=ignoredups:ignorespace
#export HISTIGNORE="&:ls:exit"

# append to the history file, don't overwrite it
shopt -s histappend

# for setting history length see HISTSIZE and HISTFILESIZE in bash(1)
HISTSIZE=2000
HISTFILESIZE=5000

# check the window size after each command and, if necessary,
# update the values of LINES and COLUMNS.
shopt -s checkwinsize

# make less more friendly for non-text input files, see lesspipe(1)
[ -x /usr/bin/lesspipe ] && eval "$(SHELL=/bin/sh lesspipe)"

# set variable identifying the chroot you work in (used in the prompt below)
if [ -z "$debian_chroot" ] && [ -r /etc/debian_chroot ]; then
    debian_chroot=$(cat /etc/debian_chroot)
fi

# set a fancy prompt (non-color, unless we know we "want" color)
case "$TERM" in
    xterm|xterm-color) color_prompt=yes;;
esac

# uncomment for a colored prompt, if the terminal has the capability; turned
# off by default to not distract the user: the focus in a terminal window
# should be on the output of commands, not on the prompt
force_color_prompt=yes

if [ -n "$force_color_prompt" ]; then
    if [ -x /usr/bin/tput ] && tput setaf 1 >&/dev/null; then
	# We have color support; assume it's compliant with Ecma-48
	# (ISO/IEC-6429). (Lack of such support is extremely rare, and such
	# a case would tend to support setf rather than setaf.)
	color_prompt=yes
    else
	color_prompt=
    fi
fi

if [ "$color_prompt" = yes ]; then
    PS1='\[\e[0;34m\]\w \$ \[\e[0;37m\]'
else
    PS1='${debian_chroot:+($debian_chroot)}\u@\w \$ '

fi
unset color_prompt force_color_prompt

# If this is an xterm set the title to user@host:dir
case "$TERM" in
xterm*|rxvt*)
    PS1="\[\e]0;${debian_chroot:+($debian_chroot)}\u@\h: \w\a\]$PS1"
    ;;
*)
    ;;
esac

if [ -f ~/.bash_aliases ]; then
	. ~/.bash_aliases
fi

# These should come after aliases:
export EDITOR=vim
export CSCOPE_EDITOR=vim

# My login items:
# screen -ls
	# interferes when scp-ing to this host... weird
xmodmap $HOME/.Xmodmap > /dev/null 2>&1
$HOME/bin/trackball.sh
# man ssh-agent; http://www.thegeekstuff.com/2008/06/perform-ssh-and-scp-without-entering-password-on-openssh/
#eval `ssh-agent`

# enable color support of ls and also add handy aliases
if [ -x /usr/bin/dircolors ]; then
	test -r ~/.dircolors && eval "$(dircolors -b ~/.dircolors)" || eval "$(dircolors -b)"
	alias ls='ls --color=auto'
	alias grep='grep --color=auto'
	alias fgrep='fgrep --color=auto'
	alias egrep='egrep --color=auto'
fi
#############################################################################
#Both the ~/.bashrc and ~/.bash_profile are scripts that might be executed when bash is invoked. The ~/.bashrc file gets executed when you run bash using an interactive shell that is not a login shell. The ~/.bash_profile only gets executed during a login shell. What does this all mean? The paragraphs below explains interactive shells, login shells, .bashrc, .bash_profile and other bash scripts that are executed during login.
#
#Login Shells (.bash_profile)
#
#A login shell is a bash shell that is started with - or --login. The following are examples that will invoke a login shell.
#
#sudo su -
#bash --login
#ssh user@host
#
#When BASH is invoked as a login shell, the following files are executed in the displayed order.
#
#/etc/profile
#~/.bash_profile
#~/.bash_login
#~/.profile
#Although ~/.bashrc is not listed here, most default ~/.bash_profile scripts run ~/.bashrc.
#
#Purely Interactive Shells (.bashrc)
#
#Interactive shells are those not invoked with -c and whose standard input and output are connected to a terminal. Interactive shells do not need to be login shells. Here are some examples that will evoke an interactive shell that is not a login shell.
#
#sudo su
#bash
#ssh user@host /path/to/command
#
#In this case of an interactive but non-login shell, only ~/.bashrc is executed. In most cases, the default ~/.bashrc script executes the system's /etc/bashrc.
#
#Be warned that you should never echo output to the screen in a ~/.bashrc file. Otherwise, commands like 'ssh user@host /path/to/command' will echo output unrelated to the command called.
#
#Non-interactive shells
#
#Non-interactive shells do not automatically execute any scripts like ~/.bashrc or ~/.bash_profile. Here are some examples of non-interactive shells.
#
#su user -c /path/to/command
#bash -c /path/to/command
