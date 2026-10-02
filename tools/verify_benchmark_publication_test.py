# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Deployment succeeds only after the public page matches the generated content."""

import io
import unittest
from unittest import mock

import verify_benchmark_publication as publication


class VerifyBenchmarkPublicationTest(unittest.TestCase):
    def test_stale_response_is_retried_until_content_matches(self):
        with mock.patch.object(publication.urllib.request, 'urlopen', side_effect=[io.BytesIO(b'old'), io.BytesIO(b'new')]), \
                mock.patch.object(publication.time, 'sleep') as sleep:
            publication.verify(b'new', 'https://example.test/benchmarks/', attempts=2)
        sleep.assert_called_once_with(15)

    def test_successful_but_wrong_content_fails(self):
        with mock.patch.object(publication.urllib.request, 'urlopen', return_value=io.BytesIO(b'old')), \
                self.assertRaisesRegex(ValueError, 'not serving'):
            publication.verify(b'new', 'https://example.test/benchmarks/', attempts=1)

    def test_network_failure_does_not_pass(self):
        with mock.patch.object(publication.urllib.request, 'urlopen', side_effect=OSError('unreachable')), \
                self.assertRaisesRegex(ValueError, 'not serving'):
            publication.verify(b'new', 'https://example.test/benchmarks/', attempts=1)


if __name__ == '__main__':
    unittest.main()
