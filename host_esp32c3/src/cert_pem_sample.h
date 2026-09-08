// SAMPLE self-signed EC P-256 cert/key (HTTPS fallback for out-of-box build).
// WARNING: THROWAWAY dummy shipped in the public repo. Generate a device-unique
//   cert_pem.h with tools/gen_cert.ps1 for real deployments -- this sample
//   private key is PUBLIC and provides no protection.
#pragma once
static const char WLB_TLS_CERT[] =
"-----BEGIN CERTIFICATE-----\n"
"MIIBsDCCAVWgAwIBAgIUc1fKdtiFWrGvk7Xob2iB0FriwwswCgYIKoZIzj0EAwIw\n"
"GjEYMBYGA1UEAwwPV2V0aGVyTG9nZ2VyQm94MB4XDTI2MDkwODA4MDEyM1oXDTM2\n"
"MDkwNTA4MDEyM1owGjEYMBYGA1UEAwwPV2V0aGVyTG9nZ2VyQm94MFkwEwYHKoZI\n"
"zj0CAQYIKoZIzj0DAQcDQgAEj2d187L2I8I+cQTUYGecZAvJ6HDIqSrCljLHfthT\n"
"G5FIK8/gGaTycCaC7DFPxyB3rcW62c8YQFKw1ELfJU6nYqN5MHcwKQYDVR0RBCIw\n"
"IIIMd2V0aGVyLmxvY2FsghBXZXRoZXJNZW1vLmxvY2FsMAkGA1UdEwQCMAAwCwYD\n"
"VR0PBAQDAgOIMBMGA1UdJQQMMAoGCCsGAQUFBwMBMB0GA1UdDgQWBBSKH3pXw4do\n"
"AffUokYl7ab4PVvD6zAKBggqhkjOPQQDAgNJADBGAiEAwbsanbxEB8dWbGZhOn/z\n"
"O28Hu8mt0UCXquEOUH4K9kQCIQCs2fvlY76Wfr74y5jrmoihzb0f09g5PzFl/VgB\n"
"ThEPnA==\n"
"-----END CERTIFICATE-----\n"
;
static const char WLB_TLS_KEY[] =
"-----BEGIN EC PRIVATE KEY-----\n"
"MHcCAQEEIEXuYh39fJBuI/gk21kdkSIQSvCQU+Z5bhXTrEIxweb1oAoGCCqGSM49\n"
"AwEHoUQDQgAEj2d187L2I8I+cQTUYGecZAvJ6HDIqSrCljLHfthTG5FIK8/gGaTy\n"
"cCaC7DFPxyB3rcW62c8YQFKw1ELfJU6nYg==\n"
"-----END EC PRIVATE KEY-----\n"
;
