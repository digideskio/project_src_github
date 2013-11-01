<?php

/**
 * @file
 * Private functions used by the Project Source: GitHub module. These functions
 * should not be used by any other module; no API compatibility is guaranteed.
 */


/**
 * Formats a release info array given a GitHub API git commit ref array
 * structure, the project info array, and a file URL.
 *
 * @param array $branch
 *   A GitHub API git commit ref array structure (e.g. commit/branch)
 *
 * @param array $info
 *   A project info array as provided by hook_project_src_info()
 *
 * @return array
 *   A release array suitable for use in the primary release theme function.
 */
function _project_src_github_format_release_info($commitref, $info) {
  // Root of the project on GitLab.
  $root = 'https://github.com/' . $info['api']['path_with_namespace'];

  // Filter $commitref['name'] here, since it's used very often below.
  $commitref['name'] = check_plain($commitref['name']);

  // Save relevant values to a release array.
  $release['name'] = $info['short_name'] . ' ' . $commitref['name'];
  $release['version'] = $release['tag'] = $commitref['name'];
  $release['date'] = strtotime($commitref['commit']['committed_date']);
  if (preg_match_all("/^\\d+\\.x-(\\d+)\\.([x\\d+])-?(.*)?/is", $commitref['name'], $version)) {
    $release['version_major'] = (int) $version[1][0];
    $release['version_patch'] = is_numeric($version[2][0]) ? (int) $version[2][0] : NULL;
    $release['version_extra'] = isset($version[3][0]) ? $version[3][0] : NULL;
  }
  $release['status'] = 'published';
  $release['release_link'] = check_url($root);

  // Details related to the download file.
  if ($file_contents = _project_src_github_file_as_string($commitref['tarball_url'])) {
    $release['download_link'] = $commitref['tarball_url'];
    $release['mdhash'] = md5($file_contents);
    $release['filesize'] = strlen($file_contents);
    $release['archive_type'] = 'tar.gz';
  }

  return $release;
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
 * Returns the contents of a file as a string given a file URL.
 *
 * @param string $file
 *   The URL of the file.
 *
 * @return string
 *   The contents of the specified file or FALSE on error.
 */
function _project_src_github_file_as_string($file) {
  $contents = FALSE;

  $ch = curl_init($file);
  curl_setopt($ch, CURLOPT_RETURNTRANSFER, TRUE);
  curl_setopt($ch, CURLOPT_CONNECTTIMEOUT, 10);
  curl_setopt($ch, CURLOPT_SSL_VERIFYPEER, FALSE);
  curl_setopt($ch, CURLOPT_FOLLOWLOCATION, true);
  $contents = curl_exec($ch);
  curl_close($ch);

  return $contents;
}
