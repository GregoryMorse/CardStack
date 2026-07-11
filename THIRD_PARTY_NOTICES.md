# Third Party Notices

## BtrieveFileSaver-Code

Source: https://github.com/erniehh/BtrieveFileSaver-Code

Included as a Git submodule at commit: `376f78ef34bebf203e3d81a1e404025268cf7067`

The repository states that it is a clone of the SourceForge Btrieve File Saver project and that the main development was done by dbcoretech.

License: GNU General Public License version 3 or later.

CardStack currently uses this source for legacy Btrieve migration support. Static linking this code means the resulting combined binary must be distributed under GPL-compatible terms. If CardStack needs a non-GPL distribution later, keep BtrieveFileSaver as a separate migration executable/process instead of linking it into the main application.
