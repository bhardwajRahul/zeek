zeek_add_plugin(
    Zeek SMB
    SOURCES SMB.cc Plugin.cc
    BIFS # SMB 1.
         smb1_com_check_directory.bif
         smb1_com_close.bif
         smb1_com_create_directory.bif
         smb1_com_echo.bif
         smb1_com_logoff_andx.bif
         smb1_com_negotiate.bif
         smb1_com_nt_create_andx.bif
         smb1_com_nt_cancel.bif
         smb1_com_query_information.bif
         smb1_com_read_andx.bif
         smb1_com_session_setup_andx.bif
         smb1_com_transaction.bif
         smb1_com_transaction_secondary.bif
         smb1_com_transaction2.bif
         smb1_com_transaction2_secondary.bif
         smb1_com_tree_connect_andx.bif
         smb1_com_tree_disconnect.bif
         smb1_com_write_andx.bif
         smb1_events.bif
         # SMB 2.
         smb2_com_close.bif
         smb2_com_create.bif
         smb2_com_negotiate.bif
         smb2_com_read.bif
         smb2_com_session_setup.bif
         smb2_com_set_info.bif
         smb2_com_tree_connect.bif
         smb2_com_tree_disconnect.bif
         smb2_com_write.bif
         smb2_com_transform_header.bif
         smb2_events.bif
         # Common boilerplate.
         events.bif
         consts.bif
         types.bif
    PAC # Common boilerplate.
        smb.pac
        smb-common.pac
        smb-strings.pac
        smb-time.pac
        smb-pipe.pac
        smb-gssapi.pac
        smb-mailslot.pac
        # SMB 1.
        smb1-protocol.pac
        smb1-com-check-directory.pac
        smb1-com-close.pac
        smb1-com-create-directory.pac
        smb1-com-echo.pac
        smb1-com-locking-andx.pac
        smb1-com-logoff-andx.pac
        smb1-com-negotiate.pac
        smb1-com-nt-cancel.pac
        smb1-com-nt-create-andx.pac
        smb1-com-nt-transact.pac
        smb1-com-query-information.pac
        smb1-com-read-andx.pac
        smb1-com-session-setup-andx.pac
        smb1-com-transaction-secondary.pac
        smb1-com-transaction.pac
        smb1-com-transaction2.pac
        smb1-com-transaction2-secondary.pac
        smb1-com-tree-connect-andx.pac
        smb1-com-tree-disconnect.pac
        smb1-com-write-andx.pac
        # SMB 2.
        smb2-protocol.pac
        smb2-com-close.pac
        smb2-com-create.pac
        smb2-com-ioctl.pac
        smb2-com-lock.pac
        smb2-com-negotiate.pac
        smb2-com-read.pac
        smb2-com-session-setup.pac
        smb2-com-set-info.pac
        smb2-com-tree-connect.pac
        smb2-com-tree-disconnect.pac
        smb2-com-write.pac
        smb2-com-transform-header.pac)
