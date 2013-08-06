# Aliases: use an alias as a kind of shortcut: when you always want to
# substitute some extended command for a simple existing command.
# New commands should be added using shell functions, below.
alias vi='/usr/bin/vim'

# Functions for new commands:
#   Use $@ to get arguments within function body:
function cd-tracing {
	cd /sys/kernel/debug/tracing
}
function unset-LD-vars {
	unset LD_LIBRARY_PATH
	unset LD_RUN_PATH
	unset LD_DEBUG
	unset LD_PRELOAD
}
function source-bashrc {
	source ~/.bashrc
}
function lst {
	ls -hlt $2 | head -n $1
}
function add-path {
	export PATH=$PATH:`pwd`
}
function keyboard-reset {
	setxkbmap -model pc105 -layout us,ru -option ",winkeys"
}
function kbd-reset {
	setxkbmap -model pc105 -layout us,ru -option ",winkeys"
}
function tex-fmt {
	fmt -s $@
}
function open-file {
	gnome-open $@
}
function reset-trackball {
	$HOME/bin/trackball.sh
}
function trackball-reset {
	$HOME/bin/trackball.sh
}
function man_html {
	man --html=google-chrome $@
}

# ssh:
function ssh-burrard {
	ssh pjh@burrard.cs.washington.edu
}
function ssh-verbena {
	ssh pjh@verbena.cs.washington.edu
}
function ssh-verbena-X {
	ssh -X pjh@verbena.cs.washington.edu
}
function sshfs-verbena {
	sshfs pjh@verbena.cs.washington.edu: $1 -o compression=yes
}
function sshfs-verbena-mac {
	sshfs pjh@verbena.cs.washington.edu: /Users/Peter/verbena -o compression=yes
}
function ssh-forkbomb {
	ssh pjh@forkbomb.cs.washington.edu
}
function ssh-attu {
	ssh pjh@attu.cs.washington.edu
}
function ssh-attu1 {
	ssh pjh@attu1.cs.washington.edu
}
function ssh-attu2 {
	ssh pjh@attu2.cs.washington.edu
}
function ssh-attu3 {
	ssh pjh@attu3.cs.washington.edu
}
function ssh-attu4 {
	ssh pjh@attu4.cs.washington.edu
}
function ssh-vole {
	ssh pjh@vole.cs.washington.edu
}
function ssh-echo {
	ssh pjh@echo.cs.washington.edu
}
function ssh-intel {
	ssh phornyac@slsshsvr.seattle.intel-research.net
}
function ssh-sampa {
	echo ssh pjh@sampa-gw.cs.washington.edu
	ssh pjh@sampa-gw.cs.washington.edu
	#echo ssh -L 3389:windows:3389 pjh@sampa-gw.cs.washington.edu
	#ssh -L 3389:windows:3389 pjh@sampa-gw.cs.washington.edu
}
function ssh-sampa-X {
	echo ssh -X pjh@sampa-gw.cs.washington.edu
	ssh -X pjh@sampa-gw.cs.washington.edu
}
function ssh-agent-start-bg {
	ssh-agent -s
	echo "ssh-add"
	echo "Run the export commands above to set SSH_AUTH_SOCK and SSH_AGENT_PID"
	echo "Then run ssh-add once from the same shell"
	echo "Beware, this ssh-agent daemon process will run until you kill it!"
}
function ssh-agent-start-fg {
	ssh-agent -s -d &
	echo "ssh-add"
	echo "Run the export commands above to set SSH_AUTH_SOCK and SSH_AGENT_PID"
	echo "Then run ssh-add once from the same shell"
	echo "This ssh-agent will run as a background job until this shell is killed"
}

# git:
function git-status {
	#alias git-stat="git-status"
	git status $@ | vi -R -
}
function git-diff-unstaged {
	#git diff $@ | vi -R -
	git diff $@ > git.diff
	vi git.diff
}
function git-diff-staged {
	#git diff --cached $@ | vi -R -
	git diff --cached $@ > git.diff
	vi git.diff
}
function git-branches {
	git branch --color -v
}
function git-branch-diagram {
	git log --graph --oneline --all
}

# cscope / ctags:
function my_cscope1 {
	find . -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \
	> cscope.files
	time cscope -q -R -b -i cscope.files
}
function my_cscope1L {
	find -L . -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \
	> cscope.files
	time cscope -q -R -b -i cscope.files
}
function my_cscope_pin {
	find . -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \
	-o -name "*.PH" -o -name "*.PLH" > cscope.files
	time cscope -q -R -b -i cscope.files
}
function my_cscope_kernel_files {
	LNX="."
	cscope_fname="cscope.files"
	echo "Writing list of files to scope/tag at $LNX/$cscope_fname"
	find $LNX                                                                \
		-path "$LNX/arch/*" ! -path "$LNX/arch/x86*" -prune -o               \
		-path "$LNX/include/asm-*" ! -path "$LNX/include/asm-generic*"       \
		                           ! -path "$LNX/include/asm-x86*" -prune -o \
		-path "$LNX/tmp*" -prune -o                                          \
		-path "$LNX/Documentation*" -prune -o                                \
		-path "$LNX/scripts*" -prune -o                                      \
		-name "*.[chxsS]" -print > $LNX/$cscope_fname
#		-path "$LNX/include/asm-*" ! -path "$LNX/include/asm-x86*" -prune -o \
#		-path "$LNX/drivers*" -prune -o                                      \
}
function my_cscope_kernel {
	my_cscope_kernel_files
	echo "Building cscope database..."
	time cscope -q -k -b -i cscope.files
	#~/scripts/my_cscope_kernel.sh
}
function my_cscope2 {
	cscope -q -R -b -i cscope.files
}
function my_cscope {
	cscope -p4 -d
}
function my_ctags1 {
	time ctags -R --fields=+fksnS .
}
function my_ctags_pin {
	#time ctags -R -h ".h.H.hh.hpp.hxx.h++.inc.def.PH.PLH.TLH" --fields=+fksnS .
	time ctags -L cscope.files --fields=+fksnS .
}
function my_ctags_kernel {
	cscope_fname="cscope.files"
	my_cscope_kernel_files
	echo "Building ctags database..."
	time ctags -L $cscope_fname --fields=+fksnS .
}

# find / grep:
function find-no-svn {
	find | grep -v 'svn' $@
}
function grep-android {
	grep -IrR --include=*.{java,c,cpp,h,rc,xml,mk} $@
}
function grep-code {
	grep -IrR --include=*.{java,c,cpp,h,s,S,rc} $@
}

# cvs:
function cvs-diff {
	cvs diff -c $@ > cvs.diff
	vi cvs.diff
}
function cvs-stat {
	cvs stat | grep -E "(\?|File:)"
}
function cvs-stat-modified {
	cvs stat | grep -E "(Added|Modified)"
}
function cvs-revert {
	cvs up -C
}

# svn:
function svn-diff {
	svn diff --diff-cmd /usr/bin/diff $@ > svn.diff
	vi svn.diff
}
function svn-diff-c {
	svn diff --diff-cmd /usr/bin/diff -x "-c" $@ > svn.diff
	vi svn.diff
}
function svn-diff-file {
	svn diff $@
}
function svn-diff-file-c {
	svn diff --diff-cmd /usr/bin/diff -x "-c" $@
	# http://svnbook.red-bean.com/nightly/en/svn.ref.svn.c.diff.html
}
function svn-stat-show-updates {
	svn stat -u $@ | egrep -v ^\\?
	#alias svn-stat-versioned='svn stat | egrep -v ^\\?'
		# http://svnbook.red-bean.com/nightly/en/svn.ref.svn.c.status.html
}

# make:
function make-bg {
	time make &> make.out &
}
function make-tail {
	tail -F make.out
}
function ant-bg {
	time ant &> ant.out &
}
function ant-tail {
	tail -F ant.out
}

# Android:
function java-set-altern-5 {
	sudo update-java-alternatives -s java-1.5.0-sun
}
function java-set-altern-6-sun {
	sudo update-java-alternatives -s java-6-sun
}
function java-set-altern-6-openjdk {
	sudo update-java-alternatives -s java-6-openjdk
}
function make-android-common {
	sudo update-java-alternatives -s java-1.5.0-sun
	. build/envsetup.sh
	lunch aosp_passion_us-eng
}
function make-android-vanilla {
	sudo update-java-alternatives -s java-1.5.0-sun
	. build/envsetup.sh
}
function make-android {
	time make -j4 &> make.out &
	#alias make-android='time make &> make.out &'
}
function make-android-tail {
	tail -F make.out
}
function make-api {
	time make update-api &> make-api.out &
}
function make-api-tail {
	tail -F make-api.out
}
function make-sdk {
	time make sdk &> make-sdk.out &
}
function make-sdk-tail {
	tail -F make-sdk.out
}
function make-sdk-post {
	chmod 755 /homes/phornyac/android-sdk-current/
	chmod 755 /homes/phornyac/android-sdk-current/tools
	#alias make-sdk-post='chmod 777 /homes/phornyac/android-sdk-current/; \
	#chmod 777 /homes/phornyac/android-sdk-current/tools; chmod 777 \
	#/homes/phornyac/android-sdk_eng.phornyac_linux-x86; chmod 777 \
	#/homes/phornyac/android-sdk_eng.phornyac_linux-x86/tools'
	#alias make-sdk-post='chmod 755 out/host/linux-x86/sdk; chmod 777 \
	#out/host/linux-x86/sdk/android-sdk_eng.phornyac_linux-x86; chmod 777 \
	#out/host/linux-x86/sdk/android-sdk_eng.phornyac_linux-x86/tools'
}
function make-fastboot-system {
	fastboot flash system out/target/product/passion/system.img
}
function make-fastboot {
	fastboot flash boot out/target/product/passion/boot.img
	fastboot flash system out/target/product/passion/system.img
	fastboot flash userdata out/target/product/passion/userdata.img
}
function make-fastboot-with-userdata {
	fastboot flash boot out/target/product/passion/boot.img
	fastboot flash system out/target/product/passion/system.img
	fastboot flash userdata out/target/product/passion/userdata.img
}
function adb-kill {
	sudo pkill adb
}
function adb-stop {
	sudo pkill adb
}
function adb-reset {
	sudo /homes/phornyac/android-sdk-current/tools/adb kill-server
	sudo /homes/phornyac/android-sdk-current/tools/adb start-server
	/homes/phornyac/android-sdk-current/tools/adb devices
}
function adb-taint {
	/homes/phornyac/android-sdk-current/tools/adb logcat | grep -iI "taint"
}
function logcat {
	/homes/phornyac/android-sdk-current/tools/adb logcat
}
function logcat-out {
	logcat -c
	logcat | tee logcat.out
}
function repo-diff {
	repo diff > repo.diff
	vi repo.diff
}
function repo-status {
	repo status > repo.status
	vi repo.status
}
function repo-branches {
	repo branches > repo.branches
	vi repo.branches
}
function repo-forall {
	repo forall -c
}
function repo-diff-forall-unstaged {
	repo forall -c 'pwd; git diff' > repo.forall.unstaged.diff
	vi repo.forall.unstaged.diff
}
function repo-forall-diff-unstaged {
	repo forall -c 'pwd; git diff' > repo.forall.unstaged.diff
	vi repo.forall.unstaged.diff
}
function repo-diff-forall-staged {
	repo forall -c 'pwd; git diff --cached' > repo.forall.staged.diff
	vi repo.forall.staged.diff
}
function repo-forall-diff-staged {
	repo forall -c 'pwd; git diff --cached' > repo.forall.staged.diff
	vi repo.forall.staged.diff
}
function repo-forall-add-tracked {
	repo forall -c 'git add -u *'
}
function repo-add-tracked-forall {
	repo forall -c 'git add -u *'
}
function repo-forall-branch {
	repo forall -c 'pwd; git branch' > repo.forall.branch
	vi repo.forall.branch
}
function repo-forall-status {
	repo forall -c 'pwd; git status' > repo.forall.status
	vi repo.forall.status
}

# django:
function django-runserver {
	python manage.py runserver
}
function django-syncdb {
	python manage.py syncdb
}
function django-shell {
	python manage.py shell
}
function django-src-rebuild {
	python setup.py install --prefix $HOME
}
function django-validate {
	python manage.py validate
}
function django-sqlall {
	python manage.py sqlall
}

# Intel:
function vtune-setup {
	source /scratch/pjh/vtune_amplifier_xe_2011/vtune_amplifier_xe_2011/amplxe-vars.sh
}

