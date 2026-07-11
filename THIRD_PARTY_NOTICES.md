# Third Party Notices

## BtrieveFileSaver-Code

Source: https://github.com/erniehh/BtrieveFileSaver-Code

Included as a Git submodule at commit: `c33c91bc1668c72bff18f14e84a8a8835c7e7739`

The repository states that it is a clone of the SourceForge Btrieve File Saver project and that the main development was done by dbcoretech.

License: GNU General Public License version 3 or later.

CardStack currently uses this source for legacy Btrieve migration support. Static linking this code means the resulting combined binary must be distributed under GPL-compatible terms. If CardStack needs a non-GPL distribution later, keep BtrieveFileSaver as a separate migration executable/process instead of linking it into the main application.
