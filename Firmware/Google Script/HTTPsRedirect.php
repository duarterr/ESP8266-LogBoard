<?php

// Check for empty post
if (empty($_POST))
	exit("Error - Post is empty");	

// Get ID argument by POST
if (isset($_POST['id']) )
	$ScriptID = $_POST['id'];
else
	exit("Error - ID not found");	

// Get VALUE argument by POST
if (isset($_POST['log']) )
	$Value = $_POST['log'];
else
	exit("Error - Log value not found");	

// Get timestamp
$TimeStamp = date("j-M-Y H:i:s");

// Create cURL post URL
$Url = 'https://script.google.com/macros/s/' . $ScriptID . '/exec?value=' . $Value;

// Create cURL instance
$curl = curl_init();
curl_setopt($curl, CURLOPT_URL, $Url);
curl_setopt($curl, CURLOPT_SSL_VERIFYPEER, false);
curl_setopt($curl, CURLOPT_RETURNTRANSFER, false);

// Send the request & save response to $Response
$Response = curl_exec($curl);

// Close request to clear up some resources
curl_close($curl);

// Append data to log.txt
//$myfile = fopen("log.txt", "a") or die ("Unable to open file!");
//fwrite($myfile, $TimeStamp .";". $Value .";". $Response ."\r\n");
//fclose($myfile);
?>