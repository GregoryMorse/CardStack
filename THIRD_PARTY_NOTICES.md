# Third Party Notices

## BtrieveFileSaver-Code

Source: https://github.com/erniehh/BtrieveFileSaver-Code

Included as a Git submodule at commit: `fc66ac90ca680b215b069bf8572468ba707d35b1`.

Integration branch: [GregoryMorse/BtrieveFileSaver-Code](https://github.com/GregoryMorse/BtrieveFileSaver-Code/tree/fix-fixed-width-btrieve-fields),
submitted upstream as [PR #1](https://github.com/erniehh/BtrieveFileSaver-Code/pull/1).
CardStack builds the pinned submodule implementation directly.

The repository states that it is a clone of the SourceForge Btrieve File Saver project and that the main development was done by dbcoretech.

License: GNU General Public License version 3 or later.

CardStack currently uses this source for legacy Btrieve migration support. Static linking this code means the resulting combined binary must be distributed under GPL-compatible terms. If CardStack needs a non-GPL distribution later, keep BtrieveFileSaver as a separate migration executable/process instead of linking it into the main application.

## Monocypher 4.0.3

Source: https://monocypher.org/download/

Monocypher is fetched at a pinned release and SHA-512 digest. CardStack uses its
Argon2id and authenticated encryption primitives under the BSD-2-Clause license.

Copyright (c) 2017-2023, Loup Vaillant

Copyright (c) 2017-2019, Michael Savage

Copyright (c) 2017-2023, Fabio Scotoni
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
OF THE POSSIBILITY OF SUCH DAMAGE.
