# @TEST-DOC: Test that the dpd.sig picks up a plaintext connection on a non-standard port.
#
# @TEST-REQUIRES: ${SCRIPTS}/have-spicy
# @TEST-EXEC: zeek -b -r ${TRACES}/postgresql/psql-aws-ssl-disable-15432.pcap %INPUT >output
# @TEST-EXEC: zeek-cut -m ts uid id.orig_h id.orig_p id.resp_h id.resp_p service  < conn.log > conn.cut
# @TEST-EXEC: zeek-cut -m < postgresql.log > postgresql.cut
#
# @TEST-EXEC: btest-diff conn.cut
# @TEST-EXEC: btest-diff postgresql.cut

@load base/protocols/conn
@load base/protocols/postgresql
