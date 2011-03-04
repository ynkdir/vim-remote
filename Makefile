
all:

dist:
	git archive --format zip --prefix vim-remote/ --output vim-remote.zip master
