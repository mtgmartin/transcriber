# Third-party software in Transcriber

All of it is downloaded at build time from its registry with a pinned version and a checked hash (`cmake/Dependencies.cmake`), or comes with JUCE.

| Part | Version | Licence | Used for |
|---|---|---|---|
| JUCE | 9.0.3 | see the JUCE licence (not checked here) | the plugin framework |
| Verovio | 6.3.0 | LGPL-3.0 | engraving the score (and the Bravura music font it carries) |
| Bravura | with Verovio | SIL Open Font Licence 1.1 | the music font |
| jsPDF | 4.2.1 | MIT | making the PDF |
| svg2pdf.js | 2.8.1 | MIT | putting the engraved pages into the PDF |
| DejaVu Serif (regular, bold, italic) | 2.37 (npm `dejavu-fonts-ttf` 2.37.3) | Bitstream Vera licence (free to use, embed and redistribute; the changes of DejaVu are public domain) | titles and the text of the notation on the page and in the PDF; embedded in every PDF that is made |
| Microsoft WebView2 SDK | 1.0.3485.44 | see the licence in the NuGet package (not checked here) | the web view of the plugin window |

The licence text of DejaVu: https://github.com/dejavu-fonts/dejavu-fonts/blob/master/LICENSE (it is also in the npm package `dejavu-fonts-ttf`, file `LICENSE`).
