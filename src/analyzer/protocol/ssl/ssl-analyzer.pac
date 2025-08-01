# Analyzer for SSL (Zeek-specific part).

refine connection SSL_Conn += {

	%include proc-certificate.pac

	function proc_client_hello(
					version : uint16, ts : double,
					client_random : bytestring,
					session_id : uint8[],
					cipher_suites16 : uint16[],
					cipher_suites24 : uint24[],
					compression_methods: uint8[]) : bool
		%{
		if ( ! version_ok(version) )
			{
			zeek_analyzer()->AnalyzerViolation(zeek::util::fmt("unsupported client SSL version 0x%04x", version));
			zeek_analyzer()->SetSkip(true);
			}
		else
			zeek_analyzer()->AnalyzerConfirmation();

		if ( ssl_client_hello )
			{
			auto cipher_vec = zeek::make_intrusive<zeek::VectorVal>(zeek::id::index_vec);

			if ( cipher_suites16 )
				{
				cipher_vec->Reserve(cipher_suites16->size());
				for ( uint32_t cipher : *cipher_suites16 )
					cipher_vec->Append(zeek::val_mgr->Count(cipher));
				}
			else
				{
				cipher_vec->Reserve(cipher_suites24->size());
				for ( auto cipher : *cipher_suites24 )
					cipher_vec->Append(zeek::val_mgr->Count(to_int()(cipher)));
				}

			auto comp_vec = zeek::make_intrusive<zeek::VectorVal>(zeek::id::index_vec);

			if ( compression_methods )
				{
				comp_vec->Reserve(compression_methods->size());
				for ( auto method : *compression_methods )
					comp_vec->Append(zeek::val_mgr->Count(method));
				}

			zeek::BifEvent::enqueue_ssl_client_hello(zeek_analyzer(), zeek_analyzer()->Conn(),
							version, record_version(), ts,
							zeek::make_intrusive<zeek::StringVal>(client_random.length(),
							                                      (const char*) client_random.data()),
							{zeek::AdoptRef{}, to_string_val(session_id)},
							std::move(cipher_vec), std::move(comp_vec));
			}

		return true;
		%}

	function proc_server_hello(
					version : uint16, v2 : bool,
					server_random : bytestring,
					session_id : uint8[],
					cipher_suites16 : uint16[],
					cipher_suites24 : uint24[],
					comp_method : uint8) : bool
		%{
		if ( ! version_ok(version) )
			{
			zeek_analyzer()->AnalyzerViolation(zeek::util::fmt("unsupported server SSL version 0x%04x", version));
			zeek_analyzer()->SetSkip(true);
			}

		if ( ssl_server_hello )
			{
			int first_cipher = 0;
			if ( cipher_suites16 && ! cipher_suites16->empty() )
				first_cipher = cipher_suites16->front();
			else if ( cipher_suites24 && ! cipher_suites24->empty() )
				first_cipher = to_int()(cipher_suites24->front());

			uint32 ts = 0;
			if ( v2 == 0 && server_random.length() >= 4 )
				ts = ntohl(*((uint32*)server_random.data()));

			zeek::BifEvent::enqueue_ssl_server_hello(zeek_analyzer(),
							zeek_analyzer()->Conn(),
							version, record_version(), ts,
							zeek::make_intrusive<zeek::StringVal>(server_random.length(),
							                                      (const char*) server_random.data()),
							{zeek::AdoptRef{}, to_string_val(session_id)},
							first_cipher, comp_method);
			}

		return true;
		%}

	function proc_v2_certificate(is_orig: bool, cert : bytestring) : bool
		%{
		vector<bytestring>* cert_list = new vector<bytestring>(1,cert);
		bool ret = proc_certificate(is_orig, zeek_analyzer()->GetFlipped(), cert_list);
		delete cert_list;
		return ret;
		%}


	function proc_v2_client_master_key(rec: SSLRecord, cipher_kind: int) : bool
		%{
		if ( ssl_established )
			zeek::BifEvent::enqueue_ssl_established(zeek_analyzer(), zeek_analyzer()->Conn());

		return true;
		%}

	function proc_handshake(rec: SSLRecord, data: bytestring, is_orig: bool) : bool
		%{
		zeek_analyzer()->SendHandshake(${rec.raw_tls_version}, data.begin(), data.end(), is_orig);
		return true;
		%}
};


refine typeattr V2Error += &let {
	proc : bool = $context.connection.proc_alert(rec, -1, error_code);
};


refine typeattr V2ClientHello += &let {
	proc : bool = $context.connection.proc_client_hello(client_version, 0,
				challenge, session_id, nullptr, ciphers, nullptr);
};

refine typeattr V2ServerHello += &let {
	check_v2 : bool = $context.connection.proc_check_v2_server_hello_version(server_version);

	proc : bool = $context.connection.proc_server_hello(server_version, true,
				conn_id_data, nullptr, nullptr, ciphers, 0) &requires(check_v2) &if(check_v2 == true);

	cert : bool = $context.connection.proc_v2_certificate(rec.is_orig, cert_data)
		&requires(proc) &requires(check_v2) &if(check_v2 == true);
};

refine typeattr V2ClientMasterKey += &let {
	proc : bool = $context.connection.proc_v2_client_master_key(rec, cipher_kind);
};

refine typeattr Handshake += &let {
	proc : bool = $context.connection.proc_handshake(rec, data, rec.is_orig);
};
