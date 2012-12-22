# .bash_profile

# Get the aliases and functions
if [ -f ~/.bashrc ]; then
	. ~/.bashrc
fi

# User specific environment and startup programs

PATH=$PATH:$HOME/bin

export PATH

export PS1="\w> "
export CSCOPE_EDITOR=/usr/bin/vim
 
#if [[ $HOSTNAME == "spcdn-lnx01" ]]; then
#        export HOME=/users2/phornyac
#elif [[ $HOSTNAME == "spcdn-lnx02" ]]; then
#        export HOME=/users3/ftp/phornyac
#elif [[ $HOSTNAME == "spcdn-lnx03" ]]; then
#        export HOME=/users1/phornyac
#fi

if [ -z "$SSH_AUTH_SOCK" ]; then
		eval `ssh-agent`
		trap "kill $SSH_AGENT_PID" 0
fi
