# ~/.bash_profile: executed by bash(1) for login shells.
# Most items should go in .bashrc; this file is for items/commands that
# affect login shells only.

# Execute .bashrc. This should be the first thing done by
# this script, because .bashrc will check if the environment variable
# PS1 (for the shell prompt) is defined before executing...
if [ -f ~/.bashrc ]; then
	. ~/.bashrc
fi

#if [[ $HOSTNAME == "spcdn-lnx01" ]]; then
#        export HOME=/users2/phornyac
#elif [[ $HOSTNAME == "spcdn-lnx02" ]]; then
#        export HOME=/users3/ftp/phornyac
#elif [[ $HOSTNAME == "spcdn-lnx03" ]]; then
#        export HOME=/users1/phornyac
#fi

#if [ -z "$SSH_AUTH_SOCK" ]; then
#		eval `ssh-agent`
#		trap "kill $SSH_AGENT_PID" 0
#fi

screen -ls

