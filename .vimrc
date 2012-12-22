" disable vi compatibility (emulation of old bugs)
set nocompatible

" Clear screen on exit:
"au VimLeave * :!clear

" http://vim.wikia.com/wiki/Keep_your_vimrc_file_clean
"   Notes for setting file-type-specific options
filetype plugin on
filetype on

"Another thing that works out of the box: Press SHIFT-K to lookup the function
"under cursor in the manpage – that’s the integrated help 

" Smart tabbing / autoindenting
" set ts=4
" set undolevels=100
" set nocompatible
" set autoindent
" set smarttab  " undoes ts=4

" Allow backspace to back over lines
" set backspace=2
" set exrc
" set shiftwidth=4
" set tabstop=4
" set cino=t0

" Give some room for errors
" set cmdheight=2

" Always limit the width of text to 80
"set textwidth=74
" Nick: "sometimes I need to manually invoke the line-balancing with 'gq'
" (see :help gq for all the other motions it recognizes)
set ruler
" Reformat the lines in the current paragraph:
"map <F7> gqip
map <F6> gqip

" Ctags / taglist stuff:
" let Tlist_Ctags_Cmd = "/usr/bin/ctags"
" let Tlist_WinWidth = 50
map <F4> :TlistToggle<cr>
map <F7> :CTAGS<cr>
map <F8> :!ctags -R --fields=+fksnS .<CR>
"function TagStuff()
	"http://stackoverflow.com/questions/954273/vim-scripting-preserve-cursor-position-and-screen-view-in-function-call
"	:mkHml    "set mark k for cursor, mark l for window
"	:CTAGS    "CTAGS regenerates the 'ctags.vim' database (see below)
"	:!ctags -R --fields=+fksnS .
"	:`lzt`k   "restore mark l for window, mark k for cursor
"endfunction
"map <F8> :exec TagStuff()<cr>
"set tags=./tags,./TAGS,tags,TAGS,../tags,../../tags,/usr/include/tags
set tags=./tags,./TAGS,tags,TAGS,/usr/include/tags
" ~/project/tags

" ctags.vim stuff: http://vim.sourceforge.net/scripts/script.php?script_id=610
let g:ctags_statusline=1
let g:ctags_title=0
let generate_tags=1
let g:ctags_regenerate=0

" Turn on line numbering. Turn it off with 'set nonu' 
"set nu 

" Set syntax on
syntax on

" Indent automatically depending on filetype
filetype indent on
set autoindent
" The best way to get filetype-specific indentation is to use ":filetype plugin
" indent on" in your vimrc. Then you can specify things like ":setl sw=4 sts=4 et"
" in .vim/ftplugin/c.vim, for example, without having to make those global for all
" files being edited and other non-C type syntaxes will get indented correctly,
" too (even lisps).

" Case sensitive search if pattern contains an uppercase letter, otherwise not
set ignorecase
set smartcase

" http://vim.wikia.com/wiki/Make_search_results_appear_in_the_middle_of_the_screen
set scrolloff=2

" Higlhight search
set hls

" Wrap text instead of being on one line
set lbr

" Change colorscheme from default to delek
"colorscheme delek

" http://vim.wikia.com/wiki/Restore_cursor_to_file_position_in_previous_editing_session
" Tell vim to remember certain things when we exit
"  '10 : marks will be remembered for up to 10 previously edited files
"  "100 : will save up to 100 lines for each register
"  :20 : up to 20 lines of command-line history will be remembered
"  % : saves and restores the buffer list
"  n... : where to save the viminfo files
set viminfo='10,\"100,:20,%,n~/.viminfo
:au BufReadPost * if line("'\"") > 1 && line("'\"") <= line("$") | exe "normal! g`\"" | endif

map <F9> :!make<CR>
" map <F9> :!make > /dev/null<CR><CR>

"" http://gergap.wordpress.com/2009/05/29/minimal-vimrc-for-cc-developers/
"" VIM Configuration File
"" Description: Optimized for C/C++ development, but useful also for other things.
"" Author: Gerhard Gappmeier
""
"" set UTF-8 encoding
"set enc=utf-8
"set fenc=utf-8
"set termencoding=utf-8
"" disable vi compatibility (emulation of old bugs)
"set nocompatible
"" use indentation of previous line
"set autoindent
"" use intelligent indentation for C
"set smartindent
"" configure tabwidth and insert spaces instead of tabs
set tabstop=4        " tab width is 4 spaces
set shiftwidth=4     " indent also with 4 spaces
"set expandtab        " expand tabs to spaces
"" wrap lines at 120 chars. 80 is somewaht antiquated with nowadays displays.
"set textwidth=120
"" turn syntax highlighting on
"set t_Co=256
"syntax on
"colorscheme wombat256
"" turn line numbers on
"set number
"" highlight matching braces
"set showmatch
set noshowmatch
"" intelligent comments
set comments=sl:/*,mb:\ *,elx:\ */
"
"" Install OmniCppComplete like described on http://vim.wikia.com/wiki/C++_code_completion
"" This offers intelligent C++ completion when typing ‘.’ ‘->’ or <C-o>
"" Load standard tag files
"set tags+=~/.vim/tags/cpp
"set tags+=~/.vim/tags/gl
"set tags+=~/.vim/tags/sdl
"set tags+=~/.vim/tags/qt4
"
"" Install DoxygenToolkit from http://www.vim.org/scripts/script.php?script_id=987
"let g:DoxygenToolkit_authorName="Gerhard Gappmeier <gerhard.gappmeier@ascolab.com>" 
"
"" Enhanced keyboard mappings
""
"" in normal mode F2 will save the file
"nmap <F2> :w<CR>
"" in insert mode F2 will exit insert, save, enters insert again
"imap <F2> <ESC>:w<CR>i
"" switch between header/source with F4
"map <F4> :e %:p:s,.h$,.X123X,:s,.cpp$,.h,:s,.X123X$,.cpp,<CR>
"" recreate tags file with F5
"map <F5> :!ctags -R –c++-kinds=+p –fields=+iaS –extra=+q .<CR>
"" create doxygen comment
"map <F6> :Dox<CR>
"" build using makeprg with <F7>
"map <F7> :make<CR>
"" build using makeprg with <S-F7>
"map <S-F7> :make clean all<CR>
"" goto definition with F12
"map <F12> <C-]>
"" in diff mode we use the spell check keys for merging
"if &diff
"  " diff settings
"  map <M-Down> ]c
"  map <M-Up> [c
"  map <M-Left> do
"  map <M-Right> dp
"  map <F9> :new<CR>:read !svn diff<CR>:set syntax=diff buftype=nofile<CR>gg
"else
"  " spell settings
"  :setlocal spell spelllang=en
"  " set the spellfile - folders must exist
"  set spellfile=~/.vim/spellfile.add
"  map <M-Down> ]s
"  map <M-Up> [s
"endif
"

