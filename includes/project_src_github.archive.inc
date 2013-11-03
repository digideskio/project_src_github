<?php

/**
 * @file
 * Project Source: GitHub callback handler for tarball repackaging.
 */


/**
 * Menu callback that returns the archive from GitHub in a way that drush and
 * other associated package handling systems can digest.
 *
 * @param $namespace
 *   The namespace (or group) under which the project exists.
 *
 * @param $project_name
 *   The name of the project.
 *
 * @param $version
 *   The version of the project.
 *
 * @return
 *   The archive as a string, suitable for streaming.
 */
function project_src_github_get_archive($namespace, $project_name, $version) {
  $github = 'https://github.com';
  $extension = $namespace . '/' . $project_name;
  $get = $github . '/' . $extension . '/archive/' . $version . '.tar.gz';

  // Attempt to load the tarball from GitHub as a string.
  if ($file = _project_src_github_file_as_string($get)) {
    // The following can potentially be time-consuming and there may be issues
    // if multiple requests attempt to be fulfilled for the same project.
    $lock_name = __FUNCTION__ . '-' . $project_name;
    if (lock_acquire($lock_name)) {
      // If we've acquired the lock, process as usual.
      $file = _project_src_github_repackage_tarball($file, $get, $project_name, $version);

      // Then release the lock.
      lock_release($lock_name);
    }
    else {
      // Continue attempting to acquire the lock indefinitely.
      while (lock_wait($lock_name)) {}

      // If we're here, the lock may be acquired.
      lock_acquire($lock_name);
      $file = _project_src_github_repackage_tarball($file);
      lock_release($lock_name);
    }

    drupal_add_http_header('Content-type', 'application/octet-stream');
    drupal_add_http_header('Content-transfer-encoding', 'binary');
    drupal_add_http_header('Content-disposition', 'attachment; filename="' . $project_name . '-' . $version . '.tar.gz"');
  }
  else {
    $file = '';
  }

  return $file;
}


/**
 * Takes the contents of a GitHub packaged tarball as a string and repackages it
 * in a form more familiar to drush and other Drupal package management tools.
 *
 * @param string $tarball
 *   The contents of a GitHub packaged tarball as a string.
 * @param string $url
 *   The source URL of the tarball.
 * @param string $pname
 *   The name of the project in question.
 * @param string $pv
 *   The version of the project in question.
 *
 * @return string
 *   The contents of a tarball suitable for use with Drush, etc.
 *
 * @todo We should really be storing/managing tarballs after repackaging.
 */
function _project_src_github_repackage_tarball($tarball, $url, $pname, $pv) {
  $file = '';

  // Save the tarball to the temp directory.
  $tar_name = md5($url . microtime()) . '.tar.gz';
  $tar_file = file_unmanaged_save_data($tarball, 'temporary://' . $tar_name);

  // Get an archiver and extract the GitHub tarball into its own directory.
  $tar_extracted = 'temporary://' . $tar_name . '.extract';
  $tar = archiver_get_archiver($tar_file);
  $tar->extract($tar_extracted);

  $source = $tar_extracted . '/' . $pname . '-' . $pv;
  $destination = 'temporary://' . $pname;

  // Drush expects a folder without version appended to it.
  if (rename($source, $destination)) {
    // Append package details to the info file.
    $info_file = $destination . '/' . $pname . '.info';
    if (file_exists($info_file)) {
      $info = _project_src_github_get_info($pname, $pv);
      file_put_contents($info_file, implode("\n", $info) . "\n", FILE_APPEND);
    }

    // Create a new tar extract and add the relevant package contents; in doing
    // so, ensure that the contents exist in the new package at the root.
    $final_extract = 'temporary://' . $tar_name . '.final';
    $new_tar = new Archive_Tar($final_extract);
    $new_tar->addModify(drupal_realpath($destination), '', drupal_realpath('temporary://'));
    $file = file_get_contents($final_extract);

    // Clean up our new archive.
    file_unmanaged_delete_recursive($destination);
    file_unmanaged_delete($final_extract);
  }

  // Clean up all remaining temporary files.
  file_unmanaged_delete($tar_file);
  file_unmanaged_delete_recursive($tar_extracted);

  return $file;
}


/**
 * Returns data to be appended to a Drupal project's .info file.
 *
 * @param string $project_name
 *   The short name of the project in question.
 * @param string $version
 *   The version of the project (e.g. 7.x-1.0 or 7.x-1.x).
 *
 * @return array
 *   Returns an array of .info strings to be appended to an existing project
 *   .info file.
 */
function _project_src_github_get_info($project_name, $version) {
  // Prepare the defaults.
  $info = array(
    'comment' => '; Project Source: GitHub packaging details.',
    'project status url' => 'project status url = ' . url('drupal/release-history', array('absolute' => TRUE)),
    'project' => 'project = ' . $project_name,
    'version' => 'version = ' . $version,
  );

  // Prepare to make use of the GitHub API client to get package date.
  $github_client = github_api_client();
  $org = variable_get('project_src_github_org', '');

  // If the version ends in an "x," we mean to get a dev release from a branch.
  if (substr($version, -1) == 'x') {
    try {
      $releases = $github_client->api('repo')->branches($org, $project_name);
      $info['version'] .= '-dev';
    }
    catch (Exception $e) {
      watchdog('project src github', 'An error occurred when attempting to load branches for %project (by %creator) from GitHub. Message returned:<br /><br />@msg', array(
        '%project' => $pname,
        '%creator' => $org,
        '@msg' => $e->getMessage(),
      ), WATCHDOG_ERROR);
      $releases = array();
    }
  }
  // Otherwise, we're getting info for a tagged release.
  else {
    try {
      $releases = $github_client->api('repo')->tags($org, $project_name);
    }
    catch (Exception $e) {
      watchdog('project src github', 'An error occurred when attempting to load tags for %project (by %creator) from GitHub. Message returned:<br /><br />@msg', array(
        '%project' => $pname,
        '%creator' => $org,
        '@msg' => $e->getMessage(),
      ), WATCHDOG_ERROR);
      $releases = array();
    }
  }

  // Iterate through all releases returned.
  foreach ($releases as $release) {
    // If the release matches our version, check the timestamp.
    if ($release['name'] == $version) {
      $commit = $github_client->api('repo')->commits()->show($org, $project_name, $release['commit']['sha']);
      $info['datestamp'] = 'datestamp = ' . strtotime($commit['commit']['author']['date']);
      break;
    }
  }

  return $info;
}
