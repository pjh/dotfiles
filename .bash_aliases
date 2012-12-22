#alias l.='ls -d .* --color=tty'
#alias ll='ls -l --color=tty'
alias source-bashrc='source ~/.bashrc'
alias ls='ls --color=tty'
alias vi='/usr/bin/vim'
alias grep='grep --color=auto'
#alias which='alias | /usr/bin/which --tty-only --read-alias --show-dot --show-tilde'
alias my_cscope1='find . -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "*.hpp" > cscope.files; time cscope -q -R -b -i cscope.files'
alias my_cscope1L='find -L . -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "*.hpp" > cscope.files; time cscope -q -R -b -i cscope.files'
alias my_cscope_kernel='~/scripts/my_cscope_kernel.sh'
alias my_cscope2='cscope -q -R -b -i cscope.files'
alias my_cscope='cscope -p4 -C -d'
alias add-path='export PATH=$PATH:`pwd`'
alias find-no-svn="find | grep -v 'svn'"
alias my_ctags1='time ctags -R --fields=+fksnS .'
#alias t='ctags -R; find . -name "*.c" -o -name "*.cc" -o -name "*.hpp" -o -name "*.hh" -o -name "*.h" -o -name "*.cpp" -o -name "*.py" -o -name "*.pl" -o -name "*.pm" | cscope -Rb -i-'
#$grepstring='time grep -IrR --exclude=svn CONFIG_INPUT * > grep/grep.CONFIG_INPUT.out'
#alias grep-no-svn="echo $grepstring"
alias grep-android="grep -IrR --include=*.{java,c,cpp,h,rc,xml,mk}"
alias grep-code="grep -IrR --include=*.{java,c,cpp,h,s,S,rc}"
alias cvs-diff='cvs diff -c > cvs.diff; vi cvs.diff'
alias cvs-stat='cvs stat | grep -E "(\?|File:)"'
alias cvs-stat-modified='cvs stat | grep -E "(Added|Modified)"'
alias cvs-revert='cvs up -C'
alias svn-diff='svn diff --diff-cmd /usr/bin/diff > svn.diff; vi svn.diff'
alias svn-diff-c='svn diff --diff-cmd /usr/bin/diff -x "-c" > svn.diff; vi svn.diff'
alias svn-diff-file='svn diff'
alias svn-diff-file-c='svn diff --diff-cmd /usr/bin/diff -x "-c"'
	# http://svnbook.red-bean.com/nightly/en/svn.ref.svn.c.diff.html
#alias svn-stat-versioned='svn stat | egrep -v ^\\?'
	# http://svnbook.red-bean.com/nightly/en/svn.ref.svn.c.status.html
alias svn-stat-show-updates='svn stat -u | egrep -v ^\\?'
alias ssh-burrard='ssh pjh@burrard.cs.washington.edu'
alias ssh-verbena='ssh pjh@verbena.cs.washington.edu'
alias ssh-forkbomb='ssh pjh@forkbomb.cs.washington.edu'
alias ssh-attu='ssh pjh@attu.cs.washington.edu'
alias ssh-intel='ssh phornyac@slsshsvr.seattle.intel-research.net'
alias ssh-sampa='echo ssh pjh@sampa-gw.dyn.cs.washington.edu; ssh pjh@sampa-gw.dyn.cs.washington.edu'
alias ssh-sampa-X='echo ssh -X pjh@sampa-gw.dyn.cs.washington.edu; ssh -X pjh@sampa-gw.dyn.cs.washington.edu'
alias ftp-disco='lftp -u disco_uw disco.dreamhosters.com'
    # password: uwdisco
alias keyboard-reset='setxkbmap -model pc105 -layout us,ru -option ",winkeys"'
alias kbd-reset='setxkbmap -model pc105 -layout us,ru -option ",winkeys"'
alias tex-fmt='fmt -s'
alias pdf='/usr/bin/evince'
#alias git-diff-unstaged='git diff > git.unstaged.diff; vi git.unstaged.diff'
#alias git-diff-staged='git diff --cached > git.staged.diff; vi git.staged.diff'
function git-diff-unstaged {
	git diff $@ | vi -R -
}
function git-diff-staged {
	git diff --cached $@ | vi -R -
}
alias git-branches='git branch --color -v'
alias git-branch-diagram='git log --graph --oneline --all'
alias open-file='gnome-open'
alias git-status='git status'
# Android aliases:
alias java-set-altern-5='sudo update-java-alternatives -s java-1.5.0-sun'
alias java-set-altern-6-sun='sudo update-java-alternatives -s java-6-sun'
alias java-set-altern-6-openjdk='sudo update-java-alternatives -s java-6-openjdk'
alias make-android-common='sudo update-java-alternatives -s java-1.5.0-sun; . build/envsetup.sh; lunch aosp_passion_us-eng'
alias make-android-vanilla='sudo update-java-alternatives -s java-1.5.0-sun; . build/envsetup.sh'
#alias make-android='time make &> make.out &'
alias make-android='time make -j4 &> make.out &'
alias make-android-tail='tail -F make.out'
alias make-api='time make update-api &> make-api.out &'
alias make-api-tail='tail -F make-api.out'
alias make-sdk='time make sdk &> make-sdk.out &'
alias make-sdk-tail='tail -F make-sdk.out'
alias make-sdk-post='chmod 755 /homes/phornyac/android-sdk-current/; chmod 755 /homes/phornyac/android-sdk-current/tools'
#alias make-sdk-post='chmod 777 /homes/phornyac/android-sdk-current/; chmod 777 /homes/phornyac/android-sdk-current/tools; chmod 777 /homes/phornyac/android-sdk_eng.phornyac_linux-x86; chmod 777 /homes/phornyac/android-sdk_eng.          phornyac_linux-x86/tools'
#alias make-sdk-post='chmod 755 out/host/linux-x86/sdk; chmod 777 out/host/linux-x86/sdk/android-sdk_eng.phornyac_linux-x86; chmod 777 out/host/linux-x86/sdk/android-sdk_eng.phornyac_linux-x86/tools'
alias make-fastboot-system='fastboot flash system out/target/product/passion/system.img'
alias make-fastboot='fastboot flash boot out/target/product/passion/boot.img; fastboot flash system out/target/product/passion/system.img; fastboot flash userdata out/target/product/passion/userdata.img'
alias make-fastboot-with-userdata='fastboot flash boot out/target/product/passion/boot.img; fastboot flash system out/target/product/passion/system.img; fastboot flash userdata out/target/product/passion/userdata.img'
alias adb-kill='sudo pkill adb'
alias adb-stop='sudo pkill adb'
alias adb-reset='sudo /homes/phornyac/android-sdk-current/tools/adb kill-server; sudo /homes/phornyac/android-sdk-current/tools/adb start-server; /homes/phornyac/android-sdk-current/tools/adb devices'
alias adb-taint='/homes/phornyac/android-sdk-current/tools/adb logcat | grep -iI "taint"'
alias logcat='/homes/phornyac/android-sdk-current/tools/adb logcat'
alias logcat-out='logcat -c; logcat | tee logcat.out'
alias repo-diff='repo diff > repo.diff; vi repo.diff'
alias repo-status='repo status > repo.status; vi repo.status'
alias repo-branches='repo branches > repo.branches; vi repo.branches'
alias repo-forall='repo forall -c'
alias repo-diff-forall-unstaged="repo forall -c 'pwd; git diff' > repo.forall.unstaged.diff; vi repo.forall.unstaged.diff"
alias repo-forall-diff-unstaged="repo forall -c 'pwd; git diff' > repo.forall.unstaged.diff; vi repo.forall.unstaged.diff"
alias repo-diff-forall-staged="repo forall -c 'pwd; git diff --cached' > repo.forall.staged.diff; vi repo.forall.staged.diff"
alias repo-forall-diff-staged="repo forall -c 'pwd; git diff --cached' > repo.forall.staged.diff; vi repo.forall.staged.diff"
alias repo-forall-add-tracked="repo forall -c 'git add -u *'"
alias repo-add-tracked-forall="repo forall -c 'git add -u *'"
alias repo-forall-branch="repo forall -c 'pwd; git branch' > repo.forall.branch; vi repo.forall.branch"
alias repo-forall-status="repo forall -c 'pwd; git status' > repo.forall.status; vi repo.forall.status"
alias reset-trackball='/homes/sys/pjh/bin/trackball.sh'
alias trackball-reset='/homes/sys/pjh/bin/trackball.sh'
alias my_man='man --html=google-chrome'
alias man_html='man --html=google-chrome'
alias django-runserver='python manage.py runserver'
alias django-syncdb='python manage.py syncdb'
alias django-shell='python manage.py shell'
alias django-src-rebuild='python setup.py install --prefix /homes/sys/pjh'
alias django-validate='python manage.py validate'
alias django-sqlall='python manage.py sqlall'
alias vtune-setup='source /scratch/pjh/vtune_amplifier_xe_2011/vtune_amplifier_xe_2011/amplxe-vars.sh'
alias lst='ls -hlt | head -n'
