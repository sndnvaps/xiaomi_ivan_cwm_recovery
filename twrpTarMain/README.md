
-------------------------------------------------------
<p>
 <code>
   	twrpTar <action> [options]

  actions: 
        -c create 
        -x extract

 options:
        -d    target directory

        
        -t    output file
	-m    skip media subfolder (has data media)
	-z    compress backup (/sbin/pigz must be present)
#ifndef TW_EXCLUDE_ENCRYPTED_BACKUPS
	-e    encrypt/decrypt backup followed by password (/sbin/openaes must be present)
	-u    encrypt using userdata encryption (must be used with -e)
#endif

       Example: 
       twrpTar -c -d /cache -t /sdcard/test.tar
       twrpTar -x -d /cache -t /sdcard/test.tar

</code>
</p>

