import pytest

import cradle


def test_req_no_uuid_in_json(session):
    req_data = \
        {'impl': {'polymorphic_id': 2147483649,
                  'missing_polymorphic_name': 'sample uuid',
                  'ptr_wrapper': {}},
         'title': 'sample title'}

    with pytest.raises(cradle.exceptions.ErrorResponseError) as excinfo:
        session.resolve_request(req_data)
    msg = excinfo.value.response['content']['error']['unknown']
    assert msg == 'no polymorphic_name found in JSON'


def test_req_unknown_uuid_in_json(session):
    req_data = \
        {'impl': {'polymorphic_id': 2147483649,
                  'polymorphic_name': 'unknown_uuid',
                  'ptr_wrapper': {}},
         'title': 'sample title'}

    with pytest.raises(cradle.exceptions.ErrorResponseError) as excinfo:
        session.resolve_request(req_data)
    msg = excinfo.value.response['content']['error']['unknown']
    assert msg == 'no request registered with uuid unknown_uuid'