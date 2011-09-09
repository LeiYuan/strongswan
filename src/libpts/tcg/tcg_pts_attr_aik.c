/*
 * Copyright (C) 2011 Sansar Choinyambuu
 * HSR Hochschule fuer Technik Rapperswil
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <http://www.fsf.org/copyleft/gpl.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include "tcg_pts_attr_aik.h"

#include <pa_tnc/pa_tnc_msg.h>
#include <bio/bio_writer.h>
#include <bio/bio_reader.h>
#include <debug.h>

typedef struct private_tcg_pts_attr_aik_t private_tcg_pts_attr_aik_t;

/**
 * Attestation Identity Key
 * see section 3.13 of PTS Protocol: Binding to TNC IF-M Specification
 *
 *					   1				   2				   3
 *   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |	 Flags	    |	Attestation Identity Key (Variable Length)  ~
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |		   Attestation Identity Key (Variable Length)		    ~
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

#define PTS_AIK_SIZE				4
#define PTS_AIK_FLAGS_NONE			0
#define PTS_AIK_FLAGS_NAKED_KEY		(1<<7)
/**
 * Private data of an tcg_pts_attr_aik_t object.
 */
struct private_tcg_pts_attr_aik_t {

	/**
	 * Public members of tcg_pts_attr_aik_t
	 */
	tcg_pts_attr_aik_t public;

	/**
	 * Attribute vendor ID
	 */
	pen_t vendor_id;

	/**
	 * Attribute type
	 */
	u_int32_t type;

	/**
	 * Attribute value
	 */
	chunk_t value;
	
	/**
	 * Noskip flag
	 */
	bool noskip_flag;

	/**
	 * AIK Certificate or Public Key
	 */
	certificate_t *aik;
};

METHOD(pa_tnc_attr_t, get_vendor_id, pen_t,
	private_tcg_pts_attr_aik_t *this)
{
	return this->vendor_id;
}

METHOD(pa_tnc_attr_t, get_type, u_int32_t,
	private_tcg_pts_attr_aik_t *this)
{
	return this->type;
}

METHOD(pa_tnc_attr_t, get_value, chunk_t,
	private_tcg_pts_attr_aik_t *this)
{
	return this->value;
}

METHOD(pa_tnc_attr_t, get_noskip_flag, bool,
	private_tcg_pts_attr_aik_t *this)
{
	return this->noskip_flag;
}

METHOD(pa_tnc_attr_t, set_noskip_flag,void,
	private_tcg_pts_attr_aik_t *this, bool noskip)
{
	this->noskip_flag = noskip;
}

METHOD(pa_tnc_attr_t, build, void,
	private_tcg_pts_attr_aik_t *this)
{
	bio_writer_t *writer;
	u_int8_t flags = PTS_AIK_FLAGS_NONE;
	cred_encoding_type_t encoding_type = CERT_ASN1_DER;
	chunk_t aik_blob;

	if (this->aik->get_type(this->aik) == CERT_TRUSTED_PUBKEY)
	{
		flags |= PTS_AIK_FLAGS_NAKED_KEY;
		encoding_type = PUBKEY_SPKI_ASN1_DER;
	}
	if (!this->aik->get_encoding(this->aik, encoding_type, &aik_blob))
	{
		DBG1(DBG_TNC, "encoding of Attestation Identity Key failed");
		aik_blob = chunk_empty;
	}
	writer = bio_writer_create(PTS_AIK_SIZE);
	writer->write_uint8(writer, flags);
	writer->write_data (writer, aik_blob);
	this->value = chunk_clone(writer->get_buf(writer));
	writer->destroy(writer);
}

METHOD(pa_tnc_attr_t, process, status_t,
	private_tcg_pts_attr_aik_t *this, u_int32_t *offset)
{
	bio_reader_t *reader;
	u_int8_t flags;
	certificate_type_t type;
	chunk_t aik_blob;
	
	if (this->value.len < PTS_AIK_SIZE)
	{
		DBG1(DBG_TNC, "insufficient data for Attestation Identity Key");
		*offset = 0;
		return FAILED;
	}
	reader = bio_reader_create(this->value);
	reader->read_uint8(reader, &flags);
	reader->read_data (reader, reader->remaining(reader), &aik_blob);

	type = (flags & PTS_AIK_FLAGS_NAKED_KEY) ? CERT_TRUSTED_PUBKEY : CERT_X509;

	this->aik = lib->creds->create(lib->creds, CRED_CERTIFICATE, type,
								   BUILD_BLOB_PEM, aik_blob, BUILD_END);
	reader->destroy(reader);

	if (!this->aik)
	{
		DBG1(DBG_TNC, "parsing of Attestation Identity Key failed");
		*offset = 0;
		return FAILED;
	}
	return SUCCESS;
}

METHOD(pa_tnc_attr_t, destroy, void,
	private_tcg_pts_attr_aik_t *this)
{
	DESTROY_IF(this->aik);
	free(this->value.ptr);
	free(this);
}

METHOD(tcg_pts_attr_aik_t, get_aik, certificate_t*,
	private_tcg_pts_attr_aik_t *this)
{
	return this->aik;
}

/**
 * Described in header.
 */
pa_tnc_attr_t *tcg_pts_attr_aik_create(certificate_t *aik)
{
	private_tcg_pts_attr_aik_t *this;

	INIT(this,
		.public = {
			.pa_tnc_attribute = {
				.get_vendor_id = _get_vendor_id,
				.get_type = _get_type,
				.get_value = _get_value,
				.get_noskip_flag = _get_noskip_flag,
				.set_noskip_flag = _set_noskip_flag,
				.build = _build,
				.process = _process,
				.destroy = _destroy,
			},
			.get_aik = _get_aik,
		},
		.vendor_id = PEN_TCG,
		.type = TCG_PTS_AIK,
		.aik = aik->get_ref(aik),
	);

	return &this->public.pa_tnc_attribute;
}


/**
 * Described in header.
 */
pa_tnc_attr_t *tcg_pts_attr_aik_create_from_data(chunk_t data)
{
	private_tcg_pts_attr_aik_t *this;

	INIT(this,
		.public = {
			.pa_tnc_attribute = {
				.get_vendor_id = _get_vendor_id,
				.get_type = _get_type,
				.get_value = _get_value,
				.get_noskip_flag = _get_noskip_flag,
				.set_noskip_flag = _set_noskip_flag,
				.build = _build,
				.process = _process,
				.destroy = _destroy,
			},
			.get_aik = _get_aik,
		},
		.vendor_id = PEN_TCG,
		.type = TCG_PTS_AIK,
		.value = chunk_clone(data),
	);

	return &this->public.pa_tnc_attribute;
}