# Example of Download Play for the Wii U

This is a homebrew example for using the Download Play library (`nn_dlp`) inside the Wii U. Under normal conditions, the DLP child would be read from `/vol/content/dlp/app/TITLE_ID.cia`, however this is done on IOSU land, so the content redirection that Aroma uses doesn't work here, since work here, since it only affects PPC. In Aroma 22, this is worked around by patching the IOSU to read the DLP child from the SD card `sd:/dlp/app/TITLE_ID.cia`.  

