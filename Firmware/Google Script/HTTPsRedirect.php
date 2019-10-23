<?php

// Check for empty post
if (empty($_POST))
	exit("-1");	

// Check if 'host' parameter was received
if (isset($_POST['host']) )
	$NewHost = $_POST['host'];
else
	exit("-1");	

// Create cURL instance	
$curl = curl_init();

// Post request will be redirected to $NewHost
curl_setopt($curl, CURLOPT_URL, $NewHost);

// Original POST content will be forwarded
curl_setopt($curl, CURLOPT_POSTFIELDS, http_build_query($_POST));

// Other cURL options
curl_setopt($curl, CURLOPT_POST, 1);
curl_setopt($curl, CURLOPT_HTTPHEADER, array ("application/x-www-form-urlencoded"));
curl_setopt($curl, CURLOPT_SSL_VERIFYPEER, false);
curl_setopt($curl, CURLOPT_RETURNTRANSFER, false);
curl_setopt($curl, CURLOPT_FOLLOWLOCATION, true);

// Send the request & save response to $Response
curl_exec($curl);

// Close request to clear up some resources
curl_close($curl);
?>