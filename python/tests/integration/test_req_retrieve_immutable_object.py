import subprocess


# Should match generated/src/cradle/version_info.hpp
def get_git_version():
    completed = subprocess.run(
        'git describe --tags --dirty --long'.split(' '),
        stdout=subprocess.PIPE)
    assert completed.returncode == 0
    output = completed.stdout.decode().strip()
    parts = output.split('-')
    if parts[-1] == 'dirty':
        result = parts[-2] + '-' + parts[-1]
    else:
        result = parts[-1]
    return result


def test_req_retrieve_immutable_object(session):
    # Request metadata
    uuid_base = 'rq_retrieve_immutable_object_func'
    git_version = get_git_version()
    uuid = f'{uuid_base}+{git_version}'
    title = 'retrieve_immutable_object'

    # Args
    url = 'https://mgh.thinknode.io/api/v1.0'
    # This immutable id comes from another test, and the resulting blob
    # is some MessagePack-encoded value
    immutable_id = '61e8255f01c0e555298e8c7360a98955'

    req_data = \
        {'impl': {'polymorphic_id': 2147483649,
                  'polymorphic_name': uuid,
                  'ptr_wrapper': {'data': {'args': {'tuple_element0': url,
                                                    'tuple_element1': session.context_id,
                                                    'tuple_element2': {'value0': immutable_id}},
                                           'uuid': {'value0': uuid}},
                                  'id': 2147483649}},
         'title': title}

    blob = session.resolve_request(req_data)
    assert blob == b'\x93\xa3abc\xa3def\xa3ghi'
